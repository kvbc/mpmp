# mpmp
Multi-purpose macro-processor

I'm too stupid and couldn't get any general-purpose macro-processor out there to work, so I decided to make my own.  
Obviously it's got lots of bugs and is quite inefficient, but it meets my needs.

# Usage
`src` - source file  
`out` - output file
```
mpmp <src> <out>
```

# Building
Nothing too fancy
```
gcc cstr.c mp.c file.c process.c
```
