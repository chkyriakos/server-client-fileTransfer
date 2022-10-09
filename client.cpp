#include <iostream>
#include <unistd.h>
#include <stdlib.h>
#include <sys/types.h>  /* sockets */
#include <sys/socket.h> /* sockets */
#include <netinet/in.h> /* Internet sockets */
#include <netdb.h>      /* gethostbyaddr */
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>

#include "common.cpp"

using namespace std;

int main(int argc, char *argv[])
{
    char *server_ip;
    int server_port;
    char *directory;

    for (int i = 1; i < argc; i++) // Reading parameteres from command line
    {
        if (strcmp("-i", argv[i]) == 0)
        {
            server_ip = argv[i + 1]; // Get server_ip
        }
        else if (strcmp("-p", argv[i]) == 0)
        {
            if (isnum(argv[i + 1])) // Checking if server_port is a number
                server_port = atoi(argv[i + 1]);
            else
            {
                printf("Error, server_port is not a number\n");
                return 1;
            }
        }
        else if (strcmp("-d", argv[i]) == 0)
        {
            directory = argv[i + 1]; // Get directory
        }
    }
    directory[strlen(directory)] = '\0';
    if (directory[strlen(directory) - 1] == '/')
    {
        directory[strlen(directory) - 1] = '\0';
    }
    string parent_dir = directory;
    string real_path;
    int substring_pos = parent_dir.find_last_of("/");
    if (substring_pos >= 0 && substring_pos <= parent_dir.length())
    {
        real_path = parent_dir.substr(parent_dir.find_last_of("/"));
    }
    else
    {
        real_path = parent_dir;
    }
    cout << real_path << endl;
    int sock;
    struct sockaddr_in server;
    struct sockaddr *serverptr = (struct sockaddr *)&server;
    struct hostent *rem;

    /* Create socket */
    if ((sock = socket(PF_INET, SOCK_STREAM, 0)) < 0)
    {
        perror("socket");
    }
    /* Find server address */
    if ((rem = gethostbyname(server_ip)) == NULL)
    {
        herror("gethostbyname");
        exit(1);
    }
    server.sin_family = AF_INET; /* Internet domain */
    memcpy(&server.sin_addr, rem->h_addr, rem->h_length);
    server.sin_port = server_port; /* Server port */
    cout << server.sin_port << endl;
    /* Initiate connection */
    if (connect(sock, serverptr, sizeof(server)) < 0)
    {
        perror("connect");
    }

    int size_block = 0;
    int block_size = 0;
    int reading;

    if ((reading = read(sock, &size_block, sizeof(int))) == -1) // Receive block_size from server
    {
        cout << "Error reading socket here!" << endl;
        return 1;
    }
    block_size = ntohl(size_block);
    cout << block_size << endl;

    int ds = htonl(strlen(directory));
    if (write(sock, &ds, sizeof(int)) == -1)
    {
        cout << "Error writing to socket" << endl;
        exit(1);
    }

    send_tokenized(sock, strlen(directory), block_size, directory); // Send directory to server

    while (1)
    {
        int l;
        int msg_length;
        char temp[block_size];
        string filepath = "";
        string file = "";
        temp[0] = '\0';
        reading = read(sock, &l, sizeof(int));
        msg_length = ntohl(l);
        if (reading > 0 && msg_length != -1)
        {
            filepath = read_tokenized(sock, msg_length, block_size); // Receive filepath from server
            string final_path = filepath.substr(filepath.find(real_path), filepath.length());
            if (final_path.at(0) == '/')
            {
                final_path = final_path.substr(1, final_path.length());
            }
            else if (strcmp(real_path.c_str(), "..") == 0)
            {
                final_path = final_path.substr(2, final_path.length());
            }

            if (mkdir("output/", 0777) == -1 && errno != EEXIST) // Create output directory
            {
                cout << "Error creating Directory" << endl;
            }
            string temp_path = "output/";
            char *path_token;
            string fp2 = final_path;
            string outpath = "output/" + fp2;
            if (strcmp(directory, "../") == 0 || strcmp(directory, "..") == 0)
            {
                outpath = "output" + fp2;
            }
            char *path_to_tok = (char *)final_path.c_str();
            path_token = strtok(path_to_tok, "/");
            while (path_token) // Create filepath structure
            {
                temp_path = temp_path + path_token;
                if (strcmp(temp_path.c_str(), outpath.c_str()) != 0)
                {
                    if (mkdir(temp_path.c_str(), 0777) == -1 && errno != EEXIST)
                    {
                        cout << "Error creating Directory" << endl;
                    }
                }
                temp_path = temp_path + "/";
                path_token = strtok(NULL, "/\0");
            }
            if (access(outpath.c_str(), F_OK) == 0) // Check if the file already exists
            {
                remove(outpath.c_str());
                cout << "Deleted file " << fp2 << endl;
            }
            FILE *file_out = fopen(outpath.c_str(), "w");

            reading = read(sock, &l, sizeof(int));
            int file_length = ntohl(l);
            if (reading > 0)
            {
                int to_read = block_size;
                for (int i = 0; i < file_length; i += block_size) // Read file bytes
                {
                    if (file_length - i < (unsigned long)block_size) // Read only bytes needed
                    {
                        to_read = file_length % block_size;
                    }
                    int total_char_read = 0;
                    while (total_char_read < to_read)
                    {
                        if ((reading = read(sock, temp + total_char_read, to_read - total_char_read)) == -1)
                        {
                            cout << "Error reading socket here!" << endl;
                            return 2;
                        }
                        total_char_read = total_char_read + reading;
                    }
                    fwrite(temp, sizeof(char), reading, file_out);
                }
            }
            fclose(file_out);
            cout << "Received file " << fp2 << endl;
        }
        else
        {
            break;
        }
    }
}