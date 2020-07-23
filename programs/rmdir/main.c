/*
    This file is part of duckOS.

    duckOS is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    duckOS is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with duckOS.  If not, see <https://www.gnu.org/licenses/>.

    Copyright (c) Byteduck 2016-2020. All rights reserved.
*/

//A simple program that prompts you to type something and repeats it back to you.

#include <stdio.h>
#include <errno.h>
#include <unistd.h>
#include <string.h>

int main(int argc, char** argv) {
	if(argc < 2) {
		printf("Missing directory operand\nUsage: rmdir DIRECTORY\n");
		return 1;
	}
	int res = rmdir(argv[1]);
	if(res == 0) return 0;
	printf("Cannot remove directory '%s': %s\n", argv[1], strerror(errno));
	return errno;
}
