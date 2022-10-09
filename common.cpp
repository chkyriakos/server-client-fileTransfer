#include <iostream>
#include <unistd.h>
#include <string.h>

using namespace std;

bool isnum(char *var) // Check if a string is only made up by numbers
{
    int length = strlen(var);
    for (int i = 0; i < length; i++)
    {
        if (!isdigit(*var))
            return false;
        *var++;
    }
    return true;
}

char *extract_filaname(char *line) // Extracts filename from the filepath
{
    char *filename;
    char *token = strtok(line, "/");
    while (token)
    {
        filename = token;
        token = strtok(NULL, "/");
    }
    return filename;
}

void send_tokenized(int sock, int size, int block_size, char *text)
{
    int subs = size / block_size;
    if ((unsigned long)(subs * block_size) < size)
        subs++;

    char substrings[subs][block_size];
    int to_write = block_size;
    for (int l = 0; l < subs; l++) // Split string to block_size substrings
    {
        if (l == (subs - 1) && (unsigned long)(subs * block_size) > size)
        {
            to_write = size % block_size;
        }
        memset(substrings[l], 0, block_size);
        memcpy(substrings[l], text + l * block_size, to_write);
        substrings[l][to_write] = '\0';
    }
    to_write = block_size;
    subs = 0;
    for (unsigned long k = 0; k < size; k += block_size) // Send string in substrings
    {
        if (size - k < (unsigned long)block_size)
        {
            to_write = size % block_size;
        }
        if (write(sock, substrings[subs], to_write) == -1)
        {
            cout << "Error writing to socket" << endl;
            exit(1);
        }
        subs++;
    }
}

string read_tokenized(int sock, int size, int block_size)
{
    int reading;
    string msg;
    char temp[block_size];
    temp[0] = '\0';
    int to_read = block_size;
    for (int i = 0; i < size; i += block_size) // Read size bytes
    {
        if (size - i < (unsigned long)block_size) // Read only bytes needed
        {
            to_read = size % block_size;
        }
        int total_char_read = 0;
        while (total_char_read < to_read)
        {
            if ((reading = read(sock, temp + total_char_read, to_read - total_char_read)) == -1)
            {
                cout << "Error reading socket here!" << endl;
                exit(1);
            }
            total_char_read = total_char_read + reading;
        }
        temp[to_read] = '\0';
        msg = msg + temp;
    }
    return msg;
}