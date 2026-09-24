"""Test actual direct-presentation hooks against Tiger's verified insertion order.

Tiger's addSubview: calls _setSuperview: (and didAddSubview:) before _setWindow:.
Synchronous fallback painting must happen before that hierarchy mutation.
"""
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]

def method(source, signature):
    start = source.index(signature)
    opening = source.index('{', start)
    depth = 1
    end = opening + 1
    while depth:
        depth += (source[end] == '{') - (source[end] == '}')
        end += 1
    return source[start:end]

SETUP = r"""
#include <objc/runtime.h>
#include <cassert>
#include <cstdio>
typedef int NSWindowOrderingMode;
static bool hierarchyMutation;
static unsigned draws;
struct WebView {
    void invalidateTigerDirectPresentation() {
        assert(!hierarchyMutation); // displayIfNeeded must not traverse an unattached child.
        ++draws;
    }
};
__attribute__((objc_root_class)) @interface NSView { @public Class isa; }
- (void)addSubview:(NSView*)view;
- (void)addSubview:(NSView*)view positioned:(NSWindowOrderingMode)place relativeTo:(NSView*)otherView;
- (void)didAddSubview:(NSView*)view;
@end
@implementation NSView
- (void)addSubview:(NSView*)view {
    if (!view) return;
    // Tiger addSubview: calls child _setSuperview: (which adds it and
    // invokes parent's didAddSubview:) before calling child _setWindow:.
    hierarchyMutation=true;
    [self didAddSubview:view];
    hierarchyMutation=false;
}
- (void)addSubview:(NSView*)view positioned:(NSWindowOrderingMode)place relativeTo:(NSView*)otherView {
    (void)place; (void)otherView;
    [self addSubview:view]; // This variant delegates to the same native operation.
}
- (void)didAddSubview:(NSView*)view { (void)view; }
@end
@interface TigerWK2View : NSView { @public WebView* _webView; }
@end
@implementation TigerWK2View
"""
ENDING = r"""
@end
int main() {
    TigerWK2View* host=(TigerWK2View*)class_createInstance(objc_getClass("TigerWK2View"),0);
    NSView* child=(NSView*)class_createInstance(objc_getClass("NSView"),0);
    WebView web; host->_webView=&web;
    [host addSubview:child]; assert(draws>=1);
    unsigned before=draws;
    [host addSubview:child positioned:1 relativeTo:nullptr]; assert(draws>before);
    before=draws;
    [host addSubview:nullptr]; assert(draws==before);
    host->_webView=nullptr;
    [host addSubview:child]; assert(draws==before);
    object_dispose(host); object_dispose(child);
    std::puts("pre-add view hooks: PASS");
}
"""
OLD_HOOK = r"""- (void)didAddSubview:(NSView*)view
{
    if (_webView)
        _webView->invalidateTigerDirectPresentation();
    [super didAddSubview:view];
}"""

@unittest.skipUnless(sys.platform == 'darwin' and shutil.which('clang++'), 'requires host Objective-C runtime and clang++')
class DirectViewHookTests(unittest.TestCase):
    def compile_and_run(self, hooks):
        with tempfile.TemporaryDirectory() as directory:
            source = Path(directory) / 'hooks.mm'
            binary = Path(directory) / 'hooks'
            source.write_text(SETUP + hooks + ENDING)
            subprocess.run(['clang++', '-std=c++20', '-Wall', '-Wextra', '-Werror',
                            '-fsanitize=address,undefined', str(source), '-lobjc', '-o', str(binary)], check=True)
            return subprocess.run([str(binary)], capture_output=True, text=True)

    def test_actual_hooks_paint_before_both_native_insertion_routes(self):
        source = (ROOT / 'spike/wk2web/TigerWK2View.mm').read_text()
        hooks = '\n'.join(method(source, signature) for signature in
                         ('- (void)addSubview:(NSView*)view\n',
                          '- (void)addSubview:(NSView*)view positioned:'))
        result = self.compile_and_run(hooks)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)

    def test_previous_hook_rejects_drawing_an_unattached_child(self):
        result = self.compile_and_run(OLD_HOOK)
        self.assertNotEqual(result.returncode, 0)
        self.assertIn('!hierarchyMutation', result.stderr)

if __name__ == '__main__':
    unittest.main()
