#include <stdio.h>
#include <string.h>
#include "libc/dce.h"
#include "libc/calls/calls.h"
#include <cosmo.h>

int main(int argc, char **argv) {
	char* exe_path = "/c/dev/cosmostuff/third_party/win_stub/win/x64/Release/win_stub.exe";
    char *argv2[2] = { exe_path, NULL };

	pid_t pid = fork();
    if (pid == 0) {
       	execvp(exe_path, argv2); // launch from parent: works
	} else {
       	execvp(exe_path, argv2); // launch from child: doesn't work
	}
	printf("Waiting for input\n");
	char c; scanf("%c", &c);
	return 0;
}
