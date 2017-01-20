#!/bin/bash

puits=false
sourc=false
protocol="tcp"

usage() { echo "Usage: $0 [-p|-s]" 1>&2; exit 1; }

while getopts "pst:" o; do 
	case "${o}" in
	        p)
	                puits=true 
	                ;;
	        s)
	                sourc=true 
	                ;;
	        *)
		        usage
		        ;;
	esac
done

if ([ "$puits" = true ] && [ "$sourc" = true ]) || ([ "$puits" = false ] && [ "$sourc" = false ])  ; then
	usage
	exit 1
fi

if [ "$puits" = true ]; then
    cd build
    ./server 
    cd ..
fi

if [ "$sourc" = true ]; then

    cd build
    ./client
    cd ..
fi

