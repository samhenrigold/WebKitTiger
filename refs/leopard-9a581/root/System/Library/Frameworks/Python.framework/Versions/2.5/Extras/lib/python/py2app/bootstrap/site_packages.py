def _site_packages():
    import site, sys, os
    paths = []
    prefixes = [sys.prefix]
    if sys.exec_prefix != sys.prefix:
        prefixes.append(sys.exec_prefix)
    for prefix in prefixes:
	if prefix == sys.prefix:
	    paths.append(os.path.join("/Library/Python", sys.version[:3], "site-packages"))
	    paths.append(os.path.join(sys.prefix, "Extras", "lib", "python"))
	else:
	    paths.append(os.path.join(prefix, 'lib', 'python' + sys.version[:3],
		'site-packages'))
    if os.path.join('.framework', '') in os.path.join(sys.prefix, ''):
        home = os.environ.get('HOME')
        if home:
            paths.append(os.path.join(home, 'Library', 'Python',
                sys.version[:3], 'site-packages'))
    for path in paths:
        site.addsitedir(path)
_site_packages()
