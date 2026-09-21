/*
 * leo64stubs.c - the rest of the symbols the Leopard x86_64 stack imports that
 * Tiger's x86_64 libSystem does not export.
 *
 * These are NOT implementations. Each one exists so the loader can finish
 * binding, and each returns a failure or a zero. They are grouped by the
 * subsystem that wants them, and none of them is on a CoreGraphics or CoreText
 * path: the closure drags in Security, LaunchServices and the CoreServices
 * sub-frameworks, and it is those that want almost all of this.
 *
 * If a 64-bit content process ever becomes real, every group below is a
 * decision: implement it, or cut the dependency that pulls it in.
 *
 * The real implementations live in leo64shim.c.
 */
#include <stdint.h>
#include <stddef.h>

/* A stub that has been called is a bug worth seeing rather than a silent zero. */
#include <stdio.h>
static void reached(const char *n) {
    static int warned;
    if (warned++ < 40) fprintf(stderr, "[leo64stubs] called: %s\n", n);
}


/* ---- CommonCrypto (24) ---- */
long CCCrypt(void) __asm__("_CCCrypt");
long CCCrypt(void) { reached("CCCrypt"); return -1; }
long CCHmac(void) __asm__("_CCHmac");
long CCHmac(void) { reached("CCHmac"); return -1; }
long CCHmacFinal(void) __asm__("_CCHmacFinal");
long CCHmacFinal(void) { reached("CCHmacFinal"); return -1; }
long CCHmacInit(void) __asm__("_CCHmacInit");
long CCHmacInit(void) { reached("CCHmacInit"); return -1; }
long CCHmacUpdate(void) __asm__("_CCHmacUpdate");
long CCHmacUpdate(void) { reached("CCHmacUpdate"); return -1; }
long CC_CAST_ecb_encrypt(void) __asm__("_CC_CAST_ecb_encrypt");
long CC_CAST_ecb_encrypt(void) { reached("CC_CAST_ecb_encrypt"); return -1; }
long CC_CAST_set_key(void) __asm__("_CC_CAST_set_key");
long CC_CAST_set_key(void) { reached("CC_CAST_set_key"); return -1; }
long CC_MD5(void) __asm__("_CC_MD5");
long CC_MD5(void) { reached("CC_MD5"); return -1; }
long CC_RC4(void) __asm__("_CC_RC4");
long CC_RC4(void) { reached("CC_RC4"); return -1; }
long CC_RC4_set_key(void) __asm__("_CC_RC4_set_key");
long CC_RC4_set_key(void) { reached("CC_RC4_set_key"); return -1; }
long CC_SHA1(void) __asm__("_CC_SHA1");
long CC_SHA1(void) { reached("CC_SHA1"); return -1; }
long CC_SHA224_Final(void) __asm__("_CC_SHA224_Final");
long CC_SHA224_Final(void) { reached("CC_SHA224_Final"); return -1; }
long CC_SHA224_Init(void) __asm__("_CC_SHA224_Init");
long CC_SHA224_Init(void) { reached("CC_SHA224_Init"); return -1; }
long CC_SHA224_Update(void) __asm__("_CC_SHA224_Update");
long CC_SHA224_Update(void) { reached("CC_SHA224_Update"); return -1; }
long aes_cc_set_iv(void) __asm__("_aes_cc_set_iv");
long aes_cc_set_iv(void) { reached("aes_cc_set_iv"); return -1; }
long aes_cc_set_key(void) __asm__("_aes_cc_set_key");
long aes_cc_set_key(void) { reached("aes_cc_set_key"); return -1; }
long aes_decrypt_cbc(void) __asm__("_aes_decrypt_cbc");
long aes_decrypt_cbc(void) { reached("aes_decrypt_cbc"); return -1; }
long aes_encrypt_cbc(void) __asm__("_aes_encrypt_cbc");
long aes_encrypt_cbc(void) { reached("aes_encrypt_cbc"); return -1; }
long osDes3Decrypt(void) __asm__("_osDes3Decrypt");
long osDes3Decrypt(void) { reached("osDes3Decrypt"); return -1; }
long osDes3Encrypt(void) __asm__("_osDes3Encrypt");
long osDes3Encrypt(void) { reached("osDes3Encrypt"); return -1; }
long osDes3Setkey(void) __asm__("_osDes3Setkey");
long osDes3Setkey(void) { reached("osDes3Setkey"); return -1; }
long osDesDecrypt(void) __asm__("_osDesDecrypt");
long osDesDecrypt(void) { reached("osDesDecrypt"); return -1; }
long osDesEncrypt(void) __asm__("_osDesEncrypt");
long osDesEncrypt(void) { reached("osDesEncrypt"); return -1; }
long osDesSetkey(void) __asm__("_osDesSetkey");
long osDesSetkey(void) { reached("osDesSetkey"); return -1; }

/* ---- quarantine (27) ---- */
long _qtn_file_alloc(void) __asm__("__qtn_file_alloc");
long _qtn_file_alloc(void) { reached("_qtn_file_alloc"); return -1; }
long _qtn_file_apply_to_fd(void) __asm__("__qtn_file_apply_to_fd");
long _qtn_file_apply_to_fd(void) { reached("_qtn_file_apply_to_fd"); return -1; }
long _qtn_file_apply_to_mount_point(void) __asm__("__qtn_file_apply_to_mount_point");
long _qtn_file_apply_to_mount_point(void) { reached("_qtn_file_apply_to_mount_point"); return -1; }
long _qtn_file_apply_to_path(void) __asm__("__qtn_file_apply_to_path");
long _qtn_file_apply_to_path(void) { reached("_qtn_file_apply_to_path"); return -1; }
long _qtn_file_free(void) __asm__("__qtn_file_free");
long _qtn_file_free(void) { reached("_qtn_file_free"); return -1; }
long _qtn_file_get_identifier(void) __asm__("__qtn_file_get_identifier");
long _qtn_file_get_identifier(void) { reached("_qtn_file_get_identifier"); return -1; }
long _qtn_file_get_metadata(void) __asm__("__qtn_file_get_metadata");
long _qtn_file_get_metadata(void) { reached("_qtn_file_get_metadata"); return -1; }
long _qtn_file_get_metadata_size(void) __asm__("__qtn_file_get_metadata_size");
long _qtn_file_get_metadata_size(void) { reached("_qtn_file_get_metadata_size"); return -1; }
long _qtn_file_get_timestamp(void) __asm__("__qtn_file_get_timestamp");
long _qtn_file_get_timestamp(void) { reached("_qtn_file_get_timestamp"); return -1; }
long _qtn_file_init_with_fd(void) __asm__("__qtn_file_init_with_fd");
long _qtn_file_init_with_fd(void) { reached("_qtn_file_init_with_fd"); return -1; }
long _qtn_file_init_with_mount_point(void) __asm__("__qtn_file_init_with_mount_point");
long _qtn_file_init_with_mount_point(void) { reached("_qtn_file_init_with_mount_point"); return -1; }
long _qtn_file_init_with_path(void) __asm__("__qtn_file_init_with_path");
long _qtn_file_init_with_path(void) { reached("_qtn_file_init_with_path"); return -1; }
long _qtn_file_set_identifier(void) __asm__("__qtn_file_set_identifier");
long _qtn_file_set_identifier(void) { reached("_qtn_file_set_identifier"); return -1; }
long _qtn_file_set_metadata(void) __asm__("__qtn_file_set_metadata");
long _qtn_file_set_metadata(void) { reached("_qtn_file_set_metadata"); return -1; }
long _qtn_file_set_timestamp(void) __asm__("__qtn_file_set_timestamp");
long _qtn_file_set_timestamp(void) { reached("_qtn_file_set_timestamp"); return -1; }
long _qtn_proc_alloc(void) __asm__("__qtn_proc_alloc");
long _qtn_proc_alloc(void) { reached("_qtn_proc_alloc"); return -1; }
long _qtn_proc_apply_to_self(void) __asm__("__qtn_proc_apply_to_self");
long _qtn_proc_apply_to_self(void) { reached("_qtn_proc_apply_to_self"); return -1; }
long _qtn_proc_free(void) __asm__("__qtn_proc_free");
long _qtn_proc_free(void) { reached("_qtn_proc_free"); return -1; }
long _qtn_proc_get_identifier(void) __asm__("__qtn_proc_get_identifier");
long _qtn_proc_get_identifier(void) { reached("_qtn_proc_get_identifier"); return -1; }
long _qtn_proc_get_metadata(void) __asm__("__qtn_proc_get_metadata");
long _qtn_proc_get_metadata(void) { reached("_qtn_proc_get_metadata"); return -1; }
long _qtn_proc_get_metadata_size(void) __asm__("__qtn_proc_get_metadata_size");
long _qtn_proc_get_metadata_size(void) { reached("_qtn_proc_get_metadata_size"); return -1; }
long _qtn_proc_get_path_exclusion_pattern(void) __asm__("__qtn_proc_get_path_exclusion_pattern");
long _qtn_proc_get_path_exclusion_pattern(void) { reached("_qtn_proc_get_path_exclusion_pattern"); return -1; }
long _qtn_proc_init_with_self(void) __asm__("__qtn_proc_init_with_self");
long _qtn_proc_init_with_self(void) { reached("_qtn_proc_init_with_self"); return -1; }
long _qtn_proc_set_identifier(void) __asm__("__qtn_proc_set_identifier");
long _qtn_proc_set_identifier(void) { reached("_qtn_proc_set_identifier"); return -1; }
long _qtn_proc_set_metadata(void) __asm__("__qtn_proc_set_metadata");
long _qtn_proc_set_metadata(void) { reached("_qtn_proc_set_metadata"); return -1; }
long _qtn_proc_set_path_exclusion_pattern(void) __asm__("__qtn_proc_set_path_exclusion_pattern");
long _qtn_proc_set_path_exclusion_pattern(void) { reached("_qtn_proc_set_path_exclusion_pattern"); return -1; }
long _qtn_xattr_name(void) __asm__("__qtn_xattr_name");
long _qtn_xattr_name(void) { reached("_qtn_xattr_name"); return -1; }

/* ---- launchd (13) ---- */
long launch_data_alloc(void) __asm__("_launch_data_alloc");
long launch_data_alloc(void) { reached("launch_data_alloc"); return -1; }
long launch_data_dict_insert(void) __asm__("_launch_data_dict_insert");
long launch_data_dict_insert(void) { reached("launch_data_dict_insert"); return -1; }
long launch_data_free(void) __asm__("_launch_data_free");
long launch_data_free(void) { reached("launch_data_free"); return -1; }
long launch_data_get_type(void) __asm__("_launch_data_get_type");
long launch_data_get_type(void) { reached("launch_data_get_type"); return -1; }
long launch_data_new_string(void) __asm__("_launch_data_new_string");
long launch_data_new_string(void) { reached("launch_data_new_string"); return -1; }
long launch_msg(void) __asm__("_launch_msg");
long launch_msg(void) { reached("launch_msg"); return -1; }
long mpm_uncork_fork(void) __asm__("_mpm_uncork_fork");
long mpm_uncork_fork(void) { reached("mpm_uncork_fork"); return -1; }
long mpm_wait(void) __asm__("_mpm_wait");
long mpm_wait(void) { reached("mpm_wait"); return -1; }
long posix_spawn(void) __asm__("_posix_spawn");
long posix_spawn(void) { reached("posix_spawn"); return -1; }
long posix_spawn_file_actions_addclose(void) __asm__("_posix_spawn_file_actions_addclose");
long posix_spawn_file_actions_addclose(void) { reached("posix_spawn_file_actions_addclose"); return -1; }
long posix_spawn_file_actions_adddup2(void) __asm__("_posix_spawn_file_actions_adddup2");
long posix_spawn_file_actions_adddup2(void) { reached("posix_spawn_file_actions_adddup2"); return -1; }
long posix_spawn_file_actions_destroy(void) __asm__("_posix_spawn_file_actions_destroy");
long posix_spawn_file_actions_destroy(void) { reached("posix_spawn_file_actions_destroy"); return -1; }
long posix_spawn_file_actions_init(void) __asm__("_posix_spawn_file_actions_init");
long posix_spawn_file_actions_init(void) { reached("posix_spawn_file_actions_init"); return -1; }

/* ---- fenv (8) ---- */
long feclearexcept(void) __asm__("_feclearexcept");
long feclearexcept(void) { reached("feclearexcept"); return -1; }
long fegetenv(void) __asm__("_fegetenv");
long fegetenv(void) { reached("fegetenv"); return -1; }
long fegetround(void) __asm__("_fegetround");
long fegetround(void) { reached("fegetround"); return -1; }
long feholdexcept(void) __asm__("_feholdexcept");
long feholdexcept(void) { reached("feholdexcept"); return -1; }
long feraiseexcept(void) __asm__("_feraiseexcept");
long feraiseexcept(void) { reached("feraiseexcept"); return -1; }
long fesetround(void) __asm__("_fesetround");
long fesetround(void) { reached("fesetround"); return -1; }
long fetestexcept(void) __asm__("_fetestexcept");
long fetestexcept(void) { reached("fetestexcept"); return -1; }
long feupdateenv(void) __asm__("_feupdateenv");
long feupdateenv(void) { reached("feupdateenv"); return -1; }

/* Data symbols and real C99, which cannot be stubs. */
void *__stack_chk_guard[1] __asm__("___stack_chk_guard") = { 0 };
void __stack_chk_fail_impl(void) __asm__("___stack_chk_fail");
void __stack_chk_fail_impl(void) { reached("__stack_chk_fail"); for (;;) ; }
static const int fe_dfl_env_storage[8] = { 0 };
const void *FE_DFL_ENV_sym __asm__("__FE_DFL_ENV") = fe_dfl_env_storage;
double nan_impl(const char *t) __asm__("_nan");
double nan_impl(const char *t) { (void)t; return __builtin_nan(""); }

/* ---- misc (17) ---- */
long OSAtomicCompareAndSwapLong(void) __asm__("_OSAtomicCompareAndSwapLong");
long OSAtomicCompareAndSwapLong(void) { reached("OSAtomicCompareAndSwapLong"); return -1; }
long _spawn_via_launchd(void) __asm__("__spawn_via_launchd");
long _spawn_via_launchd(void) { reached("_spawn_via_launchd"); return -1; }
long copyfile_state_alloc(void) __asm__("_copyfile_state_alloc");
long copyfile_state_alloc(void) { reached("copyfile_state_alloc"); return -1; }
long copyfile_state_free(void) __asm__("_copyfile_state_free");
long copyfile_state_free(void) { reached("copyfile_state_free"); return -1; }
long copyfile_state_set(void) __asm__("_copyfile_state_set");
long copyfile_state_set(void) { reached("copyfile_state_set"); return -1; }
long csops(void) __asm__("_csops");
long csops(void) { reached("csops"); return -1; }
long getaddrinfo_async_cancel(void) __asm__("_getaddrinfo_async_cancel");
long getaddrinfo_async_cancel(void) { reached("getaddrinfo_async_cancel"); return -1; }
long getfsstat64(void) __asm__("_getfsstat64");
long getfsstat64(void) { reached("getfsstat64"); return -1; }
long getiopolicy_np(void) __asm__("_getiopolicy_np");
long getiopolicy_np(void) { reached("getiopolicy_np"); return -1; }
long lstat64(void) __asm__("_lstat64");
long lstat64(void) { reached("lstat64"); return -1; }
long proc_pidpath(void) __asm__("_proc_pidpath");
long proc_pidpath(void) { reached("proc_pidpath"); return -1; }
long setiopolicy_np(void) __asm__("_setiopolicy_np");
long setiopolicy_np(void) { reached("setiopolicy_np"); return -1; }
long vm_purgable_control(void) __asm__("_vm_purgable_control");
long vm_purgable_control(void) { reached("vm_purgable_control"); return -1; }
