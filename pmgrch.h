#ifndef PMGRCH_H
#define PMGRCH_H

/* proc manager crash handler */

inline int pmgrch_sock_fd;
inline void pmgrch_signal_handler(int signum) {
    puts("--[###-0-###]-- Crashed, waiting for signal handler to do it's job...");

    char buff = 0;
    int wret = write(pmgrch_sock_fd, &buff, 1);    /* notify the crash handler */
    if (wret != 1) {
        puts("Failed to write to socket");
        signal(signum, SIG_DFL);
        raise(signum);
        return ;
    }

    int rret = read(pmgrch_sock_fd, &buff, 1);     /* wait for it to write the raport */
    if (rret != 1) {
        puts("Failed to read from socket");
        if (rret > 1)
            puts("failed more?");

        signal(signum, SIG_DFL);
        raise(signum);
        return ;
    }

    puts("Crash logged");
    signal(signum, SIG_DFL);
    raise(signum);
}

inline int pmgrch_init() {
    pid_t pmgrch_pid = getpid();
    std::string sock_path = path_pid_dir(getppid()) + "/pmgrch.sock";

    struct sockaddr_un crash_socket = {0};

    crash_socket.sun_family = AF_UNIX;
    strcpy(crash_socket.sun_path, sock_path.c_str());
    DBG("sock_path: %s", sock_path.c_str());

    ASSERT_FN(pmgrch_sock_fd = socket(AF_UNIX, SOCK_STREAM, 0));
    int conn_res = -1;
    for (int i = 0; i < 10; i++) {
        conn_res = connect(pmgrch_sock_fd, (struct sockaddr *) &crash_socket, sizeof(crash_socket));
        if (conn_res >= 0)
            break;
        sleep_ms(100);
    }
    ASSERT_FN(conn_res);
    ASSERT_FN(write(pmgrch_sock_fd, &pmgrch_pid, sizeof(pmgrch_pid)));

    struct sigaction new_action;

    /* Set up the structure to specify the new action. */
    new_action.sa_handler = pmgrch_signal_handler;
    sigemptyset(&new_action.sa_mask);
    new_action.sa_flags = SA_ONSTACK; // Use dedicated alternate signal stack

    sigaction(SIGILL,  &new_action, NULL); // 4
    sigaction(SIGTRAP, &new_action, NULL); // 5
    sigaction(SIGABRT, &new_action, NULL); // 6
    sigaction(SIGFPE,  &new_action, NULL); // 8
    sigaction(SIGSEGV, &new_action, NULL); // 11

    return 0;
}

#endif
