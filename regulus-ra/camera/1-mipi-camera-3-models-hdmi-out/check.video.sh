#!/bin/bash
videos=`ls /dev/video*`
echo ${videos}
for i in ${videos}
do
	video=`v4l2-ctl -d ${i} --all | grep -m 1 "Card type"`
	echo "${i} ${video}"
done
