cd ~/COMP3301/dev/ass3/comp3301-s4233846/a3/
./uml-kernel ubda=rootfs.ext2 mem=128m umid=comp3301 eth0=daemon ubdb=500K.img
mount none /mnt/host -t hostfs -o /home/students/s4233846
cd /mnt/host/COMP3301/dev/ass3/comp3301-s4233846/a3/
insmod ext3301.ko
mount -t ext3301 -o key=0xAA /dev/ubdb /mnt/ext3301

uml_mconsole comp3301
config ubdb=500K.img

umount /mnt/ext3301
rmmod ext3301
#poweroff