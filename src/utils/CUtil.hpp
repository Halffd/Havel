#ifndef STRING_UTILS_H
#define STRING_UTILS_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <algorithm>
#include <cctype>
#include "types.hpp"

// Function to get a substring
char* substring(const char *str, int start, int end);

// Function to replace all occurrences of a substring
char* replace(const char *str, const char *old, const char *replacement);

// Function to find the index of a substring
int indexOf(const char *str, const char *substr);

// Function to split a string by a delimiter
char** splitArr(const char *str, const char *delimiter, int *count);

// Function to free the memory allocated for the split result
void free_split(char **result, int count);

// Struct to hold the dynamic array of strings
typedef struct {
    char **tokens; // Array of strings
    int size;      // Number of tokens
    int capacity;  // Capacity of the array
} StrArray;

// Function to initialize a StrArray
StrArray* create_array();

// Function to free the StrArray
void free_array(StrArray *arr);

// Function to append a string to the dynamic array
void append(StrArray *arr, const char *token);

// Function to get a string at a specific index
const char* get(StrArray *arr, int index);

// Function to set a string at a specific index
void set(StrArray *arr, int index, const char *token);

// Function to delete a string at a specific index
void del(StrArray *arr, int index);

// Function to split a string by a delimiter (StrArray version)
StrArray* split(const char *str, const char *delimiter);

// Function to join an array of strings into a single string with a delimiter
char* join(StrArray *arr, const char *delimiter);

str ToLower(cstr txt);

#endif // STRING_UTILS_H