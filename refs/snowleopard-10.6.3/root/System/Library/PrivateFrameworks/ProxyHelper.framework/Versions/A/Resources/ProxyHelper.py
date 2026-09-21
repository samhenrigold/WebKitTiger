#
#  ProxyHelper.py
#
#  Created by gbv on 1/14/08.
#  Copyright (c) 2008-2009 Apple, Inc. All rights reserved.
#

import objc
import os
from twisted.internet import threads, reactor, defer
import utilities

# authenticateUser is the entry point.  It's an async routine that returns a Deferred object
# example call:
# def authComplete(result, string):
#     print 'Got', string, 'auth result:', result
# 
# d = ProxyHelper.authenticateUser("testUser", "password")
# d.addCallback(authComplete, "first");


def authenticateAndAuthorizeUser(user, password,service, ipAddress):
    if None == service: service = ""
    if None == ipAddress: ipAddress = ""
    if None == password: password = ""
    if None == user: user = ""
    user = utilities.Truncate(user, 2000)
    password = utilities.Truncate(password,2000)
    
    deferred = threads.deferToThread(authUserPoolHelper, user, password, service, ipAddress)
    deferred.addCallback(postAuth, user)
    return deferred
	
# we need to create an autoreleasepool on the new thread before calling the PyObjC bridge
def authUserPoolHelper(user, password,service,ipAddress):
    pool = NSAutoreleasePool.alloc().init()
    result = ProxyHelper.authenticateUser_withPassword_(user, password)
    if 0 != result:
        result = ProxyHelper.isUserAllowed_service_ipAddress_(user, service,ipAddress)
    delay = ProxyHelper.delayTimeForUser_hadAuthError_(user, (result == 0));
    return (result, delay)

def postAuth(result, user):
    if result[1] > 0:
        d = defer.Deferred()
        reactor.callLater(result[1], d.callback, result[0])
        return d
    return result[0]
	
def proxyHelperDebug(enable):
    ProxyHelper.enableDebugLog_(enable)
    
badAuthCounts = {}
if None == os.environ.get('DYLD_FRAMEWORK_PATH', None):
    path1 = "/System/Library/PrivateFrameworks/ProxyHelper.framework/"
else:
    path1 = os.environ.get('DYLD_FRAMEWORK_PATH', "") + "/ProxyHelper.framework"

path = objc.pathForFramework(path1)
objc.loadBundle("ProxyHelper", globals(),bundle_path=path)
del objc
