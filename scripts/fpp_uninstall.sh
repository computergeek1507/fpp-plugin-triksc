#!/bin/bash

# fpp-serial-event uninstall script
echo "Running fpp-serial-event uninstall Script"

BASEDIR=$(dirname $0)
cd $BASEDIR
cd ..
make clean

