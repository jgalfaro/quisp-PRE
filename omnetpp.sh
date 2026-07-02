OMNETPP_DIR="$HOME/Downloads/omnetpp-6.0.3/" 

cd "$OMNETPP_DIR" || {
    echo "Could not find OMNeT++ directory: $OMNETPP_DIR"
    exit 1
}

. ./setenv

omnetpp


