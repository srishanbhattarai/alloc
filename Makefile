test:
	g++ -g3 -O0 -Wall -Wextra -pedantic -Iinclude src/bdalloc.c test/bdalloc_test.c -fsanitize=address
