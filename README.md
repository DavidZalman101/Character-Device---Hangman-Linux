# Character Device - Hangman Linux
### The compilers frontend stages
- Lexical Analysis - Flex
- Syntax Analysis - Bison
- Semantic Analysis - Visitor Design
- Intermediate Code Generation - LLVM

## Build and run compiler
**Make use of the Makefile for an easy build**
```sh
Make
```

## Insert the module
**Make use of the Makefile for an easy build**
```sh
sudo insmod hangman.ko
```

**Check if the moduke is loaded**
```sh
lsmod | grep hangman
```

## Start playing hangman
**pick a device - hangman_0**
```sh
cat /dev/hangman_0
```
**output**
```sh
Please enter the word to be guessed
```

```sh
Please enter the word to be guessed
```

```sh
echo -n "papayas" > /dev/hangman_0
```

**output**
```sh
******
  _______
  |     |
  |
  |
  |
  |
__|__
```

```sh
echo -n "papabs" > /dev/hangman_0
```

```sh
papa*a
  _______
  |     |
  |     O
  |     |
  |
  |
__|__
```

```sh
echo -n "papayas" > /dev/hangman_0
```

```sh
papaya
  _______
  |     |
  |     O
  |     |
  |
  |
__|__
```

## Remove module and clean the build

```sh
sudo rrmod hangman && make clean
```
