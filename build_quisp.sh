#!/bin/bash

OMNETPP_DIR="$HOME/Downloads/omnetpp-6.0.3/" 
QUISP_DIR="$HOME/workspace/quisp-PRE/quisp"

cd "$OMNETPP_DIR" || {
    echo "Could not find OMNeT++ directory: $OMNETPP_DIR"
    exit 1
}

source ./setenv

cd "$QUISP_DIR" || {
    echo "Could not find QuISP directory: $QUISP_DIR"
    exit 1
}

make MODE=release clean

make MODE=release -j12 all LDFLAGS="-L$OMNETPP_DIR/lib -Wl,-rpath,$OMNETPP_DIR/lib -pthread"



