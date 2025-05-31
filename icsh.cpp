#include <iostream>
#include <sstream>
#include <string>
#include <unistd.h>
#include <fstream>
#include <csignal>
#include <fcntl.h>    // for open flags

#define WRITE_STATIC_STR(str) write(STDOUT_FILENO, str, sizeof(str))

using namespace std;

int SHELL_PID = getpid();

struct sigaction GLOBAL_SIG,CHILD_SIG;
vector<vector<string>> CMD_HIST;
int EXIT_CODE = 0;
enum class CMD {
    ECHOS,
    EXIT,
    UNKNOWN,
    JOBS,
    FG,
    BG,
};
enum class JOBS_STATUS {
    DONE,
    RUNNING,
    STOP,
    EXITED,
};

struct Job {
    int jobid;
    string command;
    mutable JOBS_STATUS status;
    mutable bool fg;

};
vector<Job> JOB_TABLE;


CMD hashString(const std::string& str) {
    if (str == "echo") return CMD::ECHOS;
    if (str == "exit")  return CMD::EXIT;
    if (str == "jobs") return CMD::JOBS;
    if (str == "fg") return CMD::FG;
    if (str == "bg") return CMD::BG;
    return CMD::UNKNOWN;
}

vector<string> prompt_to_token() {
    string input;
    getline(std::cin, input);
    istringstream my_istream;
    my_istream.str(input);
    string my_vector[255];
    vector<string> my_vector_string;
    int i = 0;
    while (my_istream) {
        my_istream >> my_vector[i];
        i++;
    }
    if (i > 0) {
        my_vector_string.reserve(i-1);
    }
    for (int j = 0; j < i-1; j++) {
        my_vector_string.push_back(my_vector[j]);
    }
    return my_vector_string;
}
int token_to_execute(vector<string> tokens) {
    vector<char*> executable;
    executable.reserve(tokens.size());
    for (auto & token : tokens) {
        executable.emplace_back(token.data());
    }
    executable.emplace_back(nullptr);
    return execvp(executable[0], executable.data());
}

void my_echo(const vector<string > &tokens) {
    if (tokens.size() == 2 && tokens[1] == "?!") {
        printf("exit code = %d\n", EXIT_CODE);
    }
    else if (tokens.size() > 1) {
        for (int i = 1; i < tokens.size(); ++i) {
            cout << tokens[i] << " ";
        }
    }
    cout << endl;
}

void job_printer(const Job &job) {
    std::string line = "\n[" + std::to_string(job.jobid) + "] " +
        (job.status == JOBS_STATUS::RUNNING ? "Running" :
         job.status == JOBS_STATUS::STOP ? "Stopped" : "Done") +
        "\t" + job.command + "\n";
    printf(line.c_str());
}

void my_job() {
    for (const auto&job: JOB_TABLE){
        if (job.status != JOBS_STATUS::DONE && !job.fg) {
            job_printer(job);
        }
    }
}
int find_job(bool fg, int rank) {
    int pos = 1;
    for (const auto&job: JOB_TABLE){
        if (job.status != JOBS_STATUS::DONE && fg == job.fg && rank == pos) {
            return job.jobid;
        }else if (job.status != JOBS_STATUS::DONE && fg == job.fg) {
            pos++;
        }
    }
    return 0;

}

void my_exit(const vector<string> &tokens) {
    if (tokens.size() == 2) {
        for (auto c : tokens[1]) {
            if (!isdigit(c)){
                return;
            }
        }
        const unsigned int num = stoi(tokens[1]);
        const unsigned char num2 = num % 256;
        exit(num2);
    }
}

void add_job(const vector<string> &tokens, const pid_t pid, const bool is_bg) {
    Job current_job  = {
        pid,
        tokens[0],
        JOBS_STATUS::RUNNING,
        !is_bg,
    };
    JOB_TABLE.push_back(current_job);
}
bool bg_exist(int pid) {
    for (auto & job : JOB_TABLE) {
        if (job.jobid == pid && !job.fg) {
            return true;
        }
    }
    return false;
}

bool is_background(vector<string> &tokens) {
    if (tokens.back() == "&") {
        tokens.pop_back();
        return true;
    }
    return false;
}

bool turn_to_bg(int pid) {
    bool can_find = false;
    int status;
    for (const auto & job : JOB_TABLE) {
        if (pid == job.jobid && !job.fg) {
            job.fg = false;
            //WRITE_STATIC_STR("GET CONTINUE");
            //tcsetpgrp(STDIN_FILENO, pid);
            kill(-pid, SIGCONT);
            //waitpid(-pid, &status, WUNTRACED);
            //tcsetpgrp(STDIN_FILENO, SHELL_PID);

            can_find = true;
        }
    }
    return can_find;
}
bool turn_to_fg(const int pid) {
    bool can_find = false;
    int status;
    for (const auto & job : JOB_TABLE) {
        if (pid == job.jobid && !job.fg) {
            job.fg = true;
            //WRITE_STATIC_STR("GET CONTINUE");
            tcsetpgrp(STDIN_FILENO, pid);
            kill(-pid, SIGCONT);
            waitpid(pid, &status, WUNTRACED);
            //printf("pass\n");
            tcsetpgrp(STDIN_FILENO, SHELL_PID);

            can_find = true;
        }
    }
    return can_find;
}
bool token_is_num(const string& token) {
    for (auto c : token) {
        if (!isdigit(c)) {
            return false;
        }
    }
    return true;
}


void ch_handler(int sig) {
    int status;
    for (auto & job : JOB_TABLE) {
        auto ch_pid = waitpid(-job.jobid, &status, WNOHANG | WUNTRACED | WCONTINUED);
        if (ch_pid <= 0) {
            continue;
        }
        if (WIFEXITED(status) || WIFSIGNALED(status)) {
            job.status = JOBS_STATUS::DONE;

        }
        else if (WIFSTOPPED(status)) {
            job.status = JOBS_STATUS::STOP;
            job.fg = false;
            if (!cin) {
                cin.clear();
            }
        }
        else if (WIFCONTINUED(status)) {
            job.status = JOBS_STATUS::RUNNING;
        }
        //if (!job.fg) job_printer(job);
        // if (job.fg) {
        //     WRITE_STATIC_STR("JOB.FG");
        // }else {
        //     WRITE_STATIC_STR("JOB.FG");
        // }
        if (!job.fg) job_printer(job);

    }
}

void exc_child(vector<string> &tokens) {
    int status;
    const bool bg = is_background(tokens);
    const auto pid = fork();
    if (pid > 0) {
        add_job(tokens, pid, bg);
    }
    if (pid == 0) {
        setpgid(0, 0);
        struct sigaction action{};
        sigemptyset(&action.sa_mask);
        action.sa_flags = 0;
        action.sa_handler = SIG_DFL;
        sigaction(SIGTSTP, &action, nullptr);
        sigaction(SIGINT, &action, nullptr);
        if (!bg) {
            tcsetpgrp(STDIN_FILENO, getpid());
        }
        int ret = token_to_execute(tokens);
        if (ret == -1) {
            cout << "invalid command";
        }
        exit(ret);
    }
    if (!bg) {
        //get child process grou[
        setpgid(pid, pid);
        tcsetpgrp(STDIN_FILENO, pid);       // hand over terminal
        waitpid(-pid, &status, WUNTRACED);  // wait on the entire PGID
        tcsetpgrp(STDIN_FILENO, SHELL_PID);

    }
}

// int my_exclaim(vector<string>tokens, const int mode) {
//     auto previous = CMD_HIST.back();
//     if (tokens.size() >= 2) {
//         previous.insert(previous.end(), tokens.begin() + 1, tokens.end());
//     }
//     if (mode == 0) {
//         for (const auto & token : previous) {
//             cout << token << " ";
//         }
//         cout << endl;
//     }
//     return 1;
// }

int my_shell_execute(vector<string> &tokens, int flag) {
    if (!tokens.empty()) {
        switch (hashString(tokens[0])) {
            case CMD::ECHOS: {
                my_echo(tokens);
                EXIT_CODE = 0;
            }
            break;
            case CMD::FG: {
                if (tokens.size() == 2) {
                    if (token_is_num(tokens[1])) {
                        const int child_pid = stoi(tokens[1]);
                        if (turn_to_fg(child_pid)) {
                            break;
                        }
                        cout << "invalid fg pid";
                        break;
                    }
                }
                cout << "invalid command";
                break;
            }
            case CMD::BG: {
                if (tokens.size() == 2) {
                    if (token_is_num(tokens[1])) {
                        const int child_pid = stoi(tokens[1]);
                        if (turn_to_bg(child_pid)) {
                            break;
                        }
                        cout << "invalid bg pid";
                        break;
                    }
                }
                cout << "invalid command";
                break;

            }
            break;
            case CMD::JOBS:
                my_job();
                break;
            case CMD::EXIT: {
                my_exit(tokens);
                cout << "invalid command";
                break;
            }
            case CMD::UNKNOWN: {
                exc_child(tokens);
            }
        }
            cout << endl;
            return flag;
    }
        cout << endl;
        return flag;
}

int contain_piping(vector<string> &tokens) { // 0 for none, 1 for > , 2 for <
    for (const auto & token : tokens) {
        if (token == ">")
            return 1;
        if (token == "<")
            return 2;
    }
    return 0;
}
tuple<vector<string>,string> redirect_cmd_and_file(vector<string> tokens) {
    string filename;
    if  (contain_piping(tokens) == 1) {
        vector<string> new_tokens;
        int flag = 0;
        for (const auto & token : tokens) {
            if (flag == 1) {
                filename = token;
                return make_tuple(new_tokens,filename);
            }
            if (token == ">" ) {
                flag = 1;
                continue;
            }
            new_tokens.push_back(token);
        }
        return make_tuple(new_tokens, filename);

    }
    if (contain_piping(tokens) == 2) {
        int flag = 0;
        int end_cmd = 0;
        vector<string> new_tokens;
        for (int i = tokens.size() -1; i >= 0; --i) {
            if (tokens[i] == "<") {
                flag = 1;
                end_cmd = i+1;
                continue;
            }
            if (flag == 1) {
                filename = tokens[i];
                printf("CAUGHT\n");
                break;
            }
        }
        for (int i = end_cmd; i < tokens.size(); ++i) {
            //cout << tokens[i] << " ";
            new_tokens.push_back(tokens[i]);
        }
        return make_tuple(new_tokens, filename);


    }
    vector<string> new_tokens;
    return make_tuple(new_tokens,filename);
}

void piping(const string &filename, vector<string> tokens) {
    // 1) Save the shell’s current STDOUT file descriptor.
    //    This duplicates STDOUT_FILENO (which normally refers to the terminal)
    //    onto a new, unused file descriptor (savedStdout).
    //    Later we can restore STDOUT_FILENO back to this saved copy.
    int savedStdout = dup(STDOUT_FILENO);
    if (savedStdout < 0) {
        perror("dup");    // If dup fails, report and return
        return;
    }

    // 2) Open (or create/truncate) the target file for redirection.
    //    O_TRUNC | O_CREAT | O_WRONLY means:
    //      - Truncate the file if it already exists,
    //      - Otherwise, create it with mode 0666,
    //      - Open it for write only.
    int fileFd = open(filename.c_str(),  O_TRUNC | O_CREAT | O_WRONLY, 0666);
    if (fileFd < 0) {
        perror("open");   // If open fails, close savedStdout and bail out
        close(savedStdout);
        return;
    }

    // 3) Redirect STDOUT to fileFd. After this dup2 call, any writes to STDOUT_FILENO
    //    will go into fileFd (i.e., into the named file) instead of the terminal.
    //
    //    Note: We do NOT need to call dup(fileFd) here—dup2 takes care of
    //    closing STDOUT_FILENO and reassigning it to refer to fileFd.
    if (dup2(fileFd, STDOUT_FILENO) < 0) {
        perror("dup2");   // On failure, clean up and restore savedStdout if needed
        close(fileFd);
        close(savedStdout);
        return;
    }
    // We no longer need the original fileFd descriptor separately,
    // because STDOUT_FILENO now “points” to that file. So close fileFd:
    close(fileFd);

    // 4) Execute the user’s command (or pipeline of commands) via my_shell_execute.
    //
    //    Because STDOUT_FILENO has been redirected to the file, any data that
    //    the child process writes to STDOUT will go into the file. Built-in commands
    //    (like “echo” or similar) also see STDOUT pointing at “fileFd”, so their output
    //    also goes into the file.
    //
    //    Internally, my_shell_execute(...) forks a child. That child inherits
    //    the currently-active STDOUT redirection, so execvp(…) will write into the file.
    //    Meanwhile, the parent (shell) is blocked in waitpid until the child finishes.
    my_shell_execute(tokens, 0);

    // 5) Restore STDOUT back to the original terminal descriptor.
    //    savedStdout holds a duplicate of what STDOUT_FILENO used to be (the controlling terminal).
    //    By calling dup2(savedStdout, STDOUT_FILENO), we undo the redirection. From this point on,
    //    any writes to STDOUT_FILENO go back to the terminal.
    if (dup2(savedStdout, STDOUT_FILENO) < 0) {
        perror("dup2 restore");
    }
    // Close the temporary savedStdout FD, since it’s no longer needed.
    close(savedStdout);
}

void readline(const string& filename){
    ifstream file(filename); // Open the file
    if (file.is_open()) {
        string line;
        while (std::getline(file, line, '\n')) {
            vector<string> tokens;
            string intermediate;
            stringstream ss(line);
            while (getline(ss,intermediate,' ')) {
                tokens.push_back(intermediate);
            }
            vector<string> check_tokens;
            if (!CMD_HIST.empty() && tokens[0] == "!!") {
                for (const auto& last : CMD_HIST[CMD_HIST.size() - 1]) {
                    check_tokens.push_back(last);
                }
                for (int i = 1; i < tokens.size(); ++i) {
                    check_tokens.push_back(tokens[i]);
                }
            }else {
                check_tokens = tokens;
            }
            CMD_HIST.push_back(check_tokens);
            printf("HAPPEN\n");
            if (contain_piping(check_tokens) != 0) {
                auto tup_output = redirect_cmd_and_file(check_tokens);
                auto redir_tokens = std::get<0>(tup_output);
                for (const auto& redir_token : redir_tokens) {
                    cout << redir_token << " ";
                }
                cout << endl;
                auto redir_file = std::get<1>(tup_output);
                //printf("filename: %s\n", redir_file.c_str());
                if (!redir_file.empty() && !redir_tokens.empty()) {
                    piping(redir_file, redir_tokens);
                }else {
                    cout << "invalid redirection" << endl;
                }
            }else {
                my_shell_execute(check_tokens, 0);
            }
        }

        file.close(); // Close the file
    } else {
        std::cerr << "Unable to open file" << std::endl;
    }


}


[[noreturn]] void process_input() {
    cout << "Starting IC shell" << endl;
    while (true) {
        tcsetpgrp(STDIN_FILENO, SHELL_PID);
        vector<string> tokens;
        //cout << "Current ID " << getpid() << endl;
        std::cout << "icsh $ ";
        tokens = prompt_to_token();
        if (!std::cin) {
            std::cin.clear();
            continue;
        }
        if (tokens.empty()) {
            continue;
        }
        vector<string> check_tokens;

         if (!CMD_HIST.empty() && tokens[0] == "!!") {
             for (const auto& last : CMD_HIST[CMD_HIST.size() - 1]) {
                 check_tokens.push_back(last);
             }
             for (int i = 1; i < tokens.size(); ++i) {
                 check_tokens.push_back(tokens[i]);
             }
             for (const auto& check_token : check_tokens) {
                 cout << check_token << " ";
             }
             cout << endl;
         }else {
             check_tokens = tokens;
             if (tokens.empty()) {
                 continue;
             }

         }
        CMD_HIST.push_back(check_tokens);
        if (contain_piping(check_tokens) != 0) {
            auto tup_output = redirect_cmd_and_file(check_tokens);
            auto redir_tokens = std::get<0>(tup_output);
            for (const auto& redir_token : redir_tokens) {
                cout << redir_token << " ";
            }
            cout << endl;
            auto redir_file = std::get<1>(tup_output);
            //printf("filename: %s\n", redir_file.c_str());
            if (!redir_file.empty() && !redir_tokens.empty()) {
                piping(redir_file, redir_tokens);
            }else {
                cout << "invalid redirection" << endl;
            }
        }else {
            my_shell_execute(check_tokens, 0);
        }
    }
}

int main(int argc, char *argv[]) {
    setpgid(getpid(),getpid());
    tcsetpgrp(STDIN_FILENO, getpgrp());
    sigemptyset(&GLOBAL_SIG.sa_mask);
    GLOBAL_SIG.sa_flags = 0;
    GLOBAL_SIG.sa_handler = SIG_IGN;
    sigaction(SIGINT, &GLOBAL_SIG, nullptr);
    sigaction(SIGTSTP, &GLOBAL_SIG, nullptr);
    sigaction(SIGTTIN, &GLOBAL_SIG, nullptr);
    sigaction(SIGTTOU, &GLOBAL_SIG, nullptr);

    sigemptyset(&CHILD_SIG.sa_mask);
    CHILD_SIG.sa_flags = 0;
    CHILD_SIG.sa_handler = ch_handler;
    sigaction(SIGCHLD, &CHILD_SIG, nullptr);



    if (argc == 2) {
        readline(argv[1]);
    }else if (argc == 1) {
        process_input();

    }else {
        cout << "Invalid input";
    }
}




