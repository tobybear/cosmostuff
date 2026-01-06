sudo sh -c "echo -1 > /proc/sys/fs/binfmt_misc/WSLInterop"
/mnt/c/dev/cosmo/bin/cosmocc -DNORMALUNIX -DLINUX -DSNDSERV -D_DEFAULT_SOURCE test.c -o test.exe -I/mnt/c/dev/cosmo/

