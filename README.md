# unix-terminal
This project implements a simple shell called crash — a (very) pared-back version of a UNIX shell.

Shells allow users to start new processes by typing in commands with their arguments. They also receive certain signals — such as keyboard interrupts — and forward them to the relevant processes.

# Instructions for use
## Prompt and parsing
The crash shell accepts inputs one line at a time from standard input. Each time a new line is being accepted, crash displays crash> followed by a single space (ASCII 32).

Each line consists of tokens deliminated by whitespace, &, or ;. Spaces, tabs, &, and ; are not tokens (and are not commands). Multiple spaces/tabs are equivalent to one. Your implementation may limit what it considers whitespace to ASCII space and horizontal tabs (characters 32 and 9).

Both ; and & terminate a command; the remainder of the line constitutes separate commands. Programs launched by commands terminated with & will run in the background.

For example, `foo     bar;glurph&&quit` has four commands: one that runs foo bar in the foreground, another that runs glurph in the background, an empty command (technically in the background), and the quit shell command.

## Commands
- `quit` takes no arguments and exits crash.
- `jobs` lists the jobs currently managed by crash that have not terminated.
- `nuke` kills all jobs in this shell with the `KILL` signal.
- `nuke 12345` kills process `12345` with the `KILL` signal if and only if it is a job in this shell that has not yet exited.
- `nuke %7` kills job %7 with the KILL signal.
- `fg %7` puts job 7 in the foreground, resuming it if suspended.
- `fg 12345` puts process `12345` in the foreground, resuming it if suspended.
- `bg %7` resumes job 7 in the background if it is suspended.
- `bg 12345` resumes process 12345 in the background if it it suspended.
- `nuke` and `bg` may take multiple arguments, each of which can be either a PID or a job ID; e.g., `nuke 12345 %7 %5 32447`.
- `foo bar glurph` runs the program `foo` with arguments `bar` and `glurph` in the foreground, inheriting the current environment.
- `foo bar glurph &` runs the program `foo` with arguments `bar` and `glurph` in the background, inheriting the current environment.


Separate commands may be separated with newlines, `;`, or `&`, so `jobs ; quit` or `foo bar & quit` each have two separate commands. Empty commands (i.e., commands that consist of no tokens) have no effect. Although `;` is just a separator, it can, at first sight appear to behave differently than `&`; for example:

- `foo & bar` runs the program `foo` in the background and immediately `bar` in the foreground.
- `foo ; bar &` runs the program `foo` the foreground, waits for `foo` to finish (or be suspended), and then runs `bar` in the background.

## Keyboard inputs
- Ctrl+C kills the foreground process (if any) via the `SIGINT` signal. If there is no foreground process, this signal is ignored.
- Ctrl+Z suspends the foreground process (if any) via the `SIGTSTP` signal. If there is no foreground process, this signal is ignored.
- Ctrl+\ sends `SIGQUIT` to the foreground process (if any). If there is no foreground process, exits crash with exit status 0.
- Ctrl+D is ignored if a foreground process exists; otherwise, it exits crash with exit status 0.
