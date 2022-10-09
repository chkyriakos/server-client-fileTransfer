#include <iostream>
#include <fstream>
#include <unistd.h>
#include <sys/types.h>  /* sockets */
#include <sys/socket.h> /* sockets */
#include <netinet/in.h> /* Internet sockets */
#include <netdb.h>      /* gethostbyaddr */
#include <string.h>
#include <pthread.h>
#include <queue>
#include <vector>
#include <sys/stat.h>
#include "dirent.h"
#include <algorithm>
#include <signal.h>

#include "common.cpp"

using namespace std;

pthread_mutex_t mtx, mtx3;
pthread_cond_t cond_nonempty;
pthread_cond_t cond_nonfull;
pthread_cond_t cond_wait_new_files;
pthread_cond_t cond_deletefd;
queue<pthread_t> thread_pool;
vector<int> fds_in_use;
vector<pthread_t> comm;
vector<pair<int, int>> ongoing;

queue<pair<string, int>> filequeue;
bool file_end = false;

// Given from command line
int block_size;
int queue_size;

bool interrupt = false;

void place(string data, int sock) // Inserting data in file queue
{
    pthread_mutex_lock(&mtx);
    while (filequeue.size() >= queue_size)
    {
        pthread_cond_wait(&cond_nonfull, &mtx); // If queue is full wait for cond_signal
    }
    filequeue.push({data, sock});
    pthread_mutex_unlock(&mtx);
}

pair<string, int> obtain() // Return the first thing in the queue
{
    pthread_mutex_lock(&mtx);
    while (filequeue.empty() && !interrupt)
    {
        pthread_cond_wait(&cond_nonempty, &mtx); // If buffer is empty wait for cond_signal
    }
    if (interrupt)
    {
        pthread_mutex_unlock(&mtx);
        return {"END", -1};
    }
    auto file = filequeue.front();
    filequeue.pop();
    pthread_mutex_unlock(&mtx);
    return file;
}

int listFiles(char *path, queue<string> *temp_files) // Find all files in a directory, save them in a queue and return the number of files
{
    DIR *dir;
    struct dirent *member;
    struct stat sb;
    if ((dir = opendir(path)) != NULL)
    {
        while ((member = readdir(dir)) != NULL)
        {
            if (strcmp(member->d_name, ".") != 0 && strcmp(member->d_name, "..") != 0)
            {
                char temp_path[1024] = "\0";
                strcat(temp_path, path);
                strcat(temp_path, "/");
                strcat(temp_path, member->d_name);
                if (stat(temp_path, &sb) == 0 && S_ISDIR(sb.st_mode))
                {
                    // cout << temp_path << " is a directory" << endl;
                    listFiles(temp_path, temp_files);
                }
                else if (stat(temp_path, &sb) == 0 && S_ISREG(sb.st_mode))
                {
                    temp_files->push(temp_path);
                }
            }
        }
        closedir(dir);
    }
    return temp_files->size();
}
void *communication(void *arg)
{
    queue<string> my_files;
    int newsock = *((int *)arg);
    int reading = 0;
    string directory;

    int size_block = htonl(block_size);
    if (write(newsock, &size_block, sizeof(int)) == -1) // Send the block_size to the client
    {
        cout << "Error writing to socket" << endl;
        exit(1);
    }

    int l;
    int directory_l;
    reading = read(newsock, &l, sizeof(int));
    directory_l = ntohl(l);
    directory = read_tokenized(newsock, directory_l, block_size); // Receive the directory from client

    directory[directory.length()] = '\0';
    if (directory[directory.length() - 1] == '/')
    {
        directory[directory.length() - 1] = '\0';
    }

    string parent_dir = directory;
    cout << directory << endl;
    int counter = listFiles((char *)directory.c_str(), &my_files);
    ongoing.push_back({newsock, counter});

    while (!my_files.empty()) // Place all files in the execution queue
    {
        place(my_files.front(), newsock);
        my_files.pop();
        pthread_cond_signal(&cond_nonempty); // Unlock obtain()
    }

    file_end = true;
    int run = 1;
    while (run) // Make sure all files has been read
    {
        pthread_mutex_lock(&mtx);
        for (int i = 0; i < ongoing.size(); i++)
        {
            if (ongoing[i].first == newsock)
            {
                if (ongoing[i].second == 0)
                {
                    run = 0;
                    file_end = false;
                }
            }
        }
        pthread_mutex_unlock(&mtx);
    }
    int finished = -1;
    int f = htonl(finished);
    if (write(newsock, &f, sizeof(int)) == -1) // Inform the client that the tranfer has finished
    {
        cout << "Error writing to socket" << endl;
        exit(1);
    }
    pthread_exit(NULL);
}

void *worker(void *)
{
    while (!interrupt)
    {
        while (!filequeue.empty() || !file_end)
        {
            auto file = obtain(); // Take the first file in the execution queue
            if (interrupt)
            {
                pthread_exit(NULL);
            }
            pthread_cond_signal(&cond_nonfull); // Unlock place()

            cout << "[" << pthread_self() << "] ";
            cout << "Obtained file <" << file.first << ", " << file.second << ">" << endl;

            pthread_mutex_lock(&mtx3);
            while (find(fds_in_use.begin(), fds_in_use.end(), file.second) != fds_in_use.end()) // If the file descriptor is already in use wait for signal
            {
                cout << "File descriptor " << file.second << " already in use" << endl;
                pthread_cond_wait(&cond_deletefd, &mtx3);
            }
            fds_in_use.push_back(file.second);

            pthread_mutex_unlock(&mtx3);

            int filepath_size = file.first.length();
            int fs = htonl(filepath_size);
            string filepath = file.first;

            if (write(file.second, &fs, sizeof(int)) == -1) // Send filepath size
            {
                cout << "Error writing to socket" << endl;
                exit(1);
            }
            send_tokenized(file.second, filepath_size, block_size, (char *)filepath.c_str()); // Tokenize filepath and send to client

            FILE *f;
            f = fopen(file.first.c_str(), "r"); // Open file
            if (f == NULL)
            {
                printf("Error opening file");
                exit(1);
            }
            int file_size = 0;
            fseek(f, 0, SEEK_END);
            file_size = ftell(f); // Size of file in bytes
            rewind(f);
            fs = htonl(file_size);
            if (write(file.second, &fs, sizeof(int)) == -1) // Send file size
            {
                cout << "Error writing to socket" << endl;
                exit(1);
            }
            const size_t buffer = file_size; // buffer is the same size as the file
            char text[buffer];
            memset(text, '\0', buffer + 1);
            if (fread(text, buffer, 1, f) >= 0) // Reading the whole file once
            {
                send_tokenized(file.second, file_size, block_size, text); // Tokenize file and send to client
                fclose(f);
            }
            else
            {
                cout << "Error reading file" << endl;
                exit(1);
            }
            cout << "[" << pthread_self() << "] ";
            cout << "Send file <" << file.first << ", " << file.second << ">" << endl;

            pthread_mutex_lock(&mtx3);
            // find and remove fd
            auto it = find(fds_in_use.begin(), fds_in_use.end(), file.second); // Find and remove file descriptor to let other workers use it
            if (it != fds_in_use.end())
            {
                fds_in_use.erase(it);
                pthread_cond_signal(&cond_deletefd);
            }
            for (int i = 0; i < ongoing.size(); i++) // Decrease ongoing files by 1
            {
                if (ongoing[i].first == file.second)
                {
                    ongoing[i].second--;
                }
            }
            pthread_mutex_unlock(&mtx3);
        }
        pthread_mutex_lock(&mtx);
        pthread_cond_wait(&cond_wait_new_files, &mtx);
        pthread_mutex_unlock(&mtx);
    }
    pthread_exit(NULL);
}

void sig_handler(int sig) // SIGINT signal handler
{
    if (sig == SIGINT)
        interrupt = true;
}

int main(int argc, char *argv[])
{
    int port;
    int thread_pool_size;
    signal(SIGINT, sig_handler);

    for (int i = 1; i < argc; i++) // Reading parameteres from command line
    {
        if (strcmp("-p", argv[i]) == 0)
        {
            if (isnum(argv[i + 1])) // Checking if port is a number
                port = atoi(argv[i + 1]);
            else
            {
                printf("Error, port is not a number\n");
                return 1;
            }
        }
        else if (strcmp("-s", argv[i]) == 0)
        {
            if (isnum(argv[i + 1])) // Checking if thread_pool_size is a number
                thread_pool_size = atoi(argv[i + 1]);
            else
            {
                printf("Error, thread_pool_size is not a number\n");
                return 1;
            }
        }
        else if (strcmp("-q", argv[i]) == 0)
        {
            if (isnum(argv[i + 1])) // Checking if queue_size is a number
                queue_size = atoi(argv[i + 1]);
            else
            {
                printf("Error, queue_size is not a number\n");
                return 1;
            }
        }
        else if (strcmp("-b", argv[i]) == 0)
        {
            if (isnum(argv[i + 1])) // Checking if block_size is a number
                block_size = atoi(argv[i + 1]);
            else
            {
                printf("Error, block_size is not a number\n");
                return 1;
            }
        }
    }

    int sock, newsock;

    unsigned int serverlen, clientlen;
    struct sockaddr_in server, client;
    struct sockaddr *serverptr, *clientptr;
    struct hostent *rem;
    char hostname[1024];
    hostname[1023] = '\0';
    gethostname(hostname, 1023);
    if ((rem = gethostbyname(hostname)) == NULL) // Get machine's IP address
    {
        herror("gethostbyname");
        exit(1);
    }

    if ((sock = socket(PF_INET, SOCK_STREAM, 0)) < 0)
    { /* Create socket */
        perror("socket");
        exit(1);
    }
    server.sin_family = PF_INET; /* Internet domain */
    memcpy(&server.sin_addr, rem->h_addr, rem->h_length);
    server.sin_port = port;
    serverptr = (struct sockaddr *)&server;
    serverlen = sizeof server;
    if (bind(sock, serverptr, serverlen) < 0)
    { /* Bind socket to address */
        perror("bind");
        exit(1);
    }
    if (getsockname(sock, serverptr, &serverlen) < 0)
    { /* Selected port */
        perror("getsockname");
        exit(1);
    }
    if (listen(sock, 5) < 0)
    { /* Listen for connections */
        perror("listen");
        exit(1);
    }

    pthread_mutex_init(&mtx, 0);
    pthread_mutex_init(&mtx3, 0);
    pthread_cond_init(&cond_nonempty, 0);
    pthread_cond_init(&cond_nonfull, 0);
    pthread_cond_init(&cond_wait_new_files, 0);
    pthread_cond_init(&cond_deletefd, 0);

    pthread_t id;
    for (int i = 0; i < thread_pool_size; i++) // Create thread_pool_size workers
    {
        pthread_create(&id, NULL, worker, NULL);
        thread_pool.push(id);
    }

    int value;
    fd_set accept_client;
    struct timeval tv;
    tv.tv_sec = 1;
    tv.tv_usec = 0;
    while (1) // Wait for connections to the server using select
    {
        if (!interrupt)
        {
            int temp_fd = sock;
            FD_ZERO(&accept_client);
            FD_SET(temp_fd, &accept_client);
            value = select(temp_fd + 1, &accept_client, NULL, NULL, &tv);
            if (value > 0)
            {
                clientptr = (struct sockaddr *)&client;
                clientlen = sizeof client;
                if ((newsock = accept(sock, clientptr, &clientlen)) < 0)
                {
                    perror("accept");
                    exit(1);
                } /* Accept connection */
                if ((rem = gethostbyaddr((char *)&client.sin_addr.s_addr,
                                         sizeof client.sin_addr.s_addr, /* Find client's address */
                                         client.sin_family)) == NULL)
                {
                    perror("gethostbyaddr");
                    exit(1);
                }
                pthread_create(&id, NULL, communication, &newsock); // Create communication thread
                pthread_cond_signal(&cond_wait_new_files);          // Wake up worker threads
                comm.push_back(id);
            }
        }
        else
        {
            // Unlock, terminate, and wait threads to finish
            pthread_cond_broadcast(&cond_nonempty);
            pthread_cond_broadcast(&cond_wait_new_files);
            while (!thread_pool.empty())
            {
                pthread_join(thread_pool.front(), NULL);
                thread_pool.pop();
            }
            while (!comm.empty())
            {
                pthread_join(comm.back(), NULL);
                comm.pop_back();
            }
            break;
        }
    }
    close(sock);
}