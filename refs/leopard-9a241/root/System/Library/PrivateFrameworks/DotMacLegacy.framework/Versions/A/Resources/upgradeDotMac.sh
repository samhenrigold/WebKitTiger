#!/bin/bash

#
# $Id$
#
# Copyright (c) 2004 Apple Computer Inc.
# All rights reserved.
#

productVers=`sw_vers -productVersion`

echo $productVers | grep ^10.5 > /dev/null
if [[ "$?" == 0 ]]; then
## Leopard
    echo "This script is unnecessary on Leopard Systems."
    exit 1
else
## Tiger
    echo $productVers | grep ^10.4 > /dev/null
    if [[ "$?" == 0 ]]; then
        buildMinor=`echo $productVers | sed -e 's/10.4.//'`
        if [[ "$buildMinor" < "7" ]]; then
            echo "Your OS build does not support the .Mac transition. Please use Software Update to upgrade to Mac OS X 10.4.7 or higher"
            exit 1
        fi
    fi
fi

defaults read com.apple.DotMacSync SyncConfigVersion > /dev/null 2>&1
if [[ "$?" != "0" ]]; then
##
## Switch from old .Mac -> New .Mac
    echo "This machine has not been switched over to the new .Mac servers."
    read -p "Type 'y' to switch to the new .Mac server (y/N): " answer
    if [[ !( "$answer" == "y" || "$answer" == "Y" ) ]]; then
        exit 1
    fi
  
    killall "System Preferences" > /dev/null 2>&1
  
    defaults write com.apple.DotMacSync SyncConfigVersion 104
    /System/Library/CoreServices/dotmacsyncclient --getconfig

    echo "==============================="
    echo "You have been updated to use the new .Mac servers."
    echo ""

else
##
## Switch from New .Mac -> Old .Mac
    echo "Config info found. This computer has already been set up with .Mac"
    read -p "If you would like to switch back to the old .Mac server, type 'y' (y/N): " answer
    if [[ !( "$answer" == "y" || "$answer" == "Y" ) ]]; then
        exit 1
    fi

    killall "System Preferences" > /dev/null 2>&1
    defaults delete com.apple.DotMacSync SyncConfigVersion
    /System/Library/CoreServices/dotmacsyncclient --getconfig

    unregisterApp=`mdfind "UnregisterDotMac" | grep UnregisterDotMac$ | head -1`
    if [ "$?" == "0" ]; then
        $unregisterApp
    fi

    echo "==============================="
    echo "You have been switched back to the Old .Mac server"
    echo "To complete this process, you must do the following:"
    echo ""
    echo " 1. Go to the sync tab of the .Mac pref pane"
    echo " 2. Uncheck all dataclasses that are currently checked"
    echo " 3. Check all dataclasses that were previously checked"
    echo " 4. Sync away!"
    echo ""

fi

exit 0
