# mpmp
Multi-purpose macro-processor

I'm too stupid and couldn't get any general-purpose macro-processor out there to work, so I decided to make my own.  
Obviously it's got lots of bugs and is quite inefficient, but it meets my needs.

Sooner or later I will run some tests.
If I find any bugs, I will do my best to fix them.

# Usage
`src` - source file  
`out` - output file
```
mpmp <src> <out>
```

# Building
Nothing too fancy
```
gcc mp.c file.c process.c cstr.c -o mpmp -std=c11 -Wno-format -Wall -Wextra -pedantic
```
