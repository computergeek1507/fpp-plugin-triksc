#!/bin/sh

echo "Running fpp-serial-event PreStart Script"

BASEDIR=$(dirname $0)
cd $BASEDIR
cd ..
make
