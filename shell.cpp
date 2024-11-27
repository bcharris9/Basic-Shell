#include <iostream>

#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#include <fcntl.h>
#include <vector>
#include <string>
#include "Tokenizer.h"

// all the basic colours for a shell prompt
#define RED     "\033[1;31m"
#define GREEN	"\033[1;32m"
#define YELLOW  "\033[1;33m"
#define BLUE	"\033[1;34m"
#define WHITE	"\033[1;37m"
#define NC      "\033[0m"

using namespace std;

int main () {
    
    string prev_dir = "";  //initialize previous directory as 
    char cwd[4096];    //Buffer

    for (;;) {
        //creates command line
        cout << GREEN << "Shell$" << NC << " ";
        
        char *user = getenv("USER");
        time_t current_time;
        time(&current_time);
        string time_string = ctime(&current_time);
        time_string = time_string.substr(4, 15);
        
        if (getcwd(cwd, sizeof(cwd)) != NULL) {
            //Print the user, time, and current working directory in the desired format
            cout << GREEN << time_string << " " << user << NC<< ":" << BLUE << cwd << NC << "$ " ; 
        } 
        else {
            perror("getcwd() error");
        }

        //get user inputted command
        string input;
        getline(cin, input);

        if (input == "exit") {  //print exit message and break out of infinite loop
            cout << RED << "Now exiting shell..." << endl << "Goodbye" << NC << endl;
            break;
        }
   
        //get tokenized commands from user input
        Tokenizer tknr(input);
        if (tknr.hasError()) {  //continue to next prompt if input had an error
            continue;
        }
        /*
         // print out every command token-by-token on individual lines
         // prints to cerr to avoid influencing autograder
         for (auto cmd : tknr.commands) {
             for (auto str : cmd->args) {
                 cerr << "|" << str << "| ";
             }
             if (cmd->hasInput()) {
                 cerr << "in< " << cmd->in_file << " ";
             }
             if (cmd->hasOutput()) {
                 cerr << "out> " << cmd->out_file << " ";
             }
             cerr << endl;
        }*/
        
        //handling change directory commands
        if (tknr.commands.size() > 0 && tknr.commands.at(0)->args.at(0) == "cd") {
            string target_dir;
            
            //Case: "cd" with no arguments -> go to home directory
            if (tknr.commands.at(0)->args.size() == 1) {
                target_dir = getenv("HOME");
            } 
            
            //Case: "cd [directory]" or "cd -"
            else {
                target_dir = tknr.commands.at(0)->args.at(1);
                
                //Case: "cd -"
                if (target_dir == "-") {
                    if (prev_dir.empty()) {
                        cerr << "cd: no previous directory" << endl;
                        continue;
                    } 
                    //Go back to the previous directory
                    else {
                        target_dir = prev_dir;
                    }
                }
            }

            //Save the current directory as the previous one
            prev_dir = cwd;

            //Changes to target directory
            if (chdir(target_dir.c_str()) != 0) {
                perror("chdir");
            }
            continue;
        }


        int num_commands = tknr.commands.size();  //Number of commands in the pipeline
        int num_pipes = num_commands - 1;  //Number of pipes needed between commands
        vector<int> pipefds(2 * num_pipes);  //Hold pipe file descriptors

        //Create pipes
        for (int i = 0; i < num_pipes; ++i) {
            if (pipe(pipefds.data() + i * 2) == -1) {
                perror("pipe");
                exit(1);
            }
        }

        //Fork and execute each command in the pipeline
        for (int i = 0; i < num_commands; ++i) {
            pid_t pid = fork();
            
            //Child process
            if (pid == 0) {
                
                //Input redirection
                if (tknr.commands.at(i)->hasInput()) {
                    int fd_in = open(tknr.commands.at(i)->in_file.c_str(), O_RDONLY);
                    if (fd_in < 0) {
                        perror("open (input redirection)");
                        exit(1);
                    }
                    dup2(fd_in, STDIN_FILENO);  //Redirect input
                    close(fd_in);
                }

                //Output redirection
                if (tknr.commands.at(i)->hasOutput()) {
                    int fd_out = open(tknr.commands.at(i)->out_file.c_str(), O_RDWR | O_CREAT | O_TRUNC, 00644);
                    if (fd_out < 0) {
                        perror("open (output redirection)");
                        exit(1);
                    }
                    dup2(fd_out, STDOUT_FILENO);  //Redirect output
                    close(fd_out);
                }

                //Set up pipes
                if (i > 0) {
                    dup2(pipefds[(i - 1) * 2], STDIN_FILENO);  //Input from previous pipe
                }
                if (i < num_pipes) {
                    dup2(pipefds[i * 2 + 1], STDOUT_FILENO);  //Output to next pipe
                }

                //Close all pipe file descriptors after duplicating
                for (int j = 0; j < 2 * num_pipes; ++j) {
                    close(pipefds[j]);
                }

                //Prepare arguments for execvp
                vector<char*> args;
                
                for (auto &arg : tknr.commands.at(i)->args) {
                    args.push_back(const_cast<char*>(arg.c_str()));
                }
                args.push_back(nullptr);

                //Execute the command
                if (execvp(args[0], args.data()) < 0) {
                    perror("execvp");
                    exit(2);
                }
            } 
            
            //Fork error
            else if (pid < 0) {
                perror("fork");
                exit(1);
            }
        }

        //Parent process: close all pipes
        for (int i = 0; i < 2 * num_pipes; ++i) {
            close(pipefds[i]);
        }

        //Wait for all children to finish
        for (int i = 0; i < num_commands; ++i) {
            int status;
            wait(&status);
        }
    }
}