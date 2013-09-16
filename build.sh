#!/bin/bash
echo "===== HELPER SCRIPT FOR COMP3301 Assignment 3 ====="
echo "===== RUNNING MAKE ======"
make
OUT=$?
if [ $OUT -eq 0 ];then
	echo "===== MAKE COMPLETE ====="
	echo "===== MAKING FS IMAGE ===="
	rm -f 500K.img
	dd if=/dev/zero of=500K.img bs=500K count=0 seek=1
	y | mke2fs 500K.img
	echo ""
	echo "==== MADE FS ON 500K.img ===="
else
	echo "==== MAKE FAILED ====="
	echo "==== COULD NOT BUILD FS IMAGE ===="
fi
