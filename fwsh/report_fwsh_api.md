# fwsh (Firmware Mini Shell) API 技術分析報告

本報告只根據 `/fwsh` 目前實際存在的內容進行分析。分析優先順序為 source code、header file、build system、script、README/comment、實際呼叫流程。本次更新沿用既有 report 的兩階段結構：第一階段做 Codebase Trace，第二階段整理 Architecture / API Technical Report；重點補上可驗證的 execution flow、callback chain、ownership / lifecycle、resource flow、error path 與風險分析。

以下內容分成：

- `# Direct Observation`：可直接從目前程式碼、header 或 Makefile 驗證。
- `# Conservative Inference`：只基於現有呼叫關係或 POSIX 行為的保守推論，會明確標示。
- 若無法從現有內容確認，會標示「目前程式碼中未觀察到」或「無法從現有內容確認」。

---

## 第一階段：Codebase Trace

### 1. Project Structure

#### # Direct Observation

| 類別 | 檔案 | 角色 |
|---|---|---|
| source file | `src/main.c` | 程式入口，依序呼叫 `shell_init()`、`shell_run()`、`shell_cleanup()`。 |
| source file | `src/shell.c` | global `ShellState g_shell`、signal handlers、prompt 建立、GNU Readline REPL、history 管理、parse/execute/free 串接。 |
| source file | `src/parser.c` | 內部 `Lexer`、quote-aware word reader、`parse_line()`、`free_pipeline()`；將輸入字串解析成 `Pipeline` / `Cmd`。 |
| source file | `src/executor.c` | process execution engine；處理 built-in direct execution、pipe、fork、dup2、redirection、execvp、waitpid、background execution。 |
| source file | `src/builtin.c` | built-in command dispatch table 與實作：`cd`、`pwd`、`exit/quit`、`help`、`history`、`clear`、`hexdump`、`crc32`、`memmap`。 |
| header file | `include/shell.h` | 定義 `_POSIX_C_SOURCE`、版本/顏色 macro、限制值、`Cmd`、`Pipeline`、`ShellState`、shell lifecycle API。 |
| header file | `include/parser.h` | 宣告 `parse_line()`、`free_pipeline()`，並註明 `parse_line` 會用 `strdup()` 配置字串，需 `free_pipeline()` 釋放。 |
| header file | `include/executor.h` | 宣告 `execute_pipeline()`。 |
| header file | `include/builtin.h` | 宣告 `is_builtin()`、`exec_builtin()`。 |
| build system | `Makefile` | 使用 `gcc`、`-std=c11`、`-Iinclude`、`-lreadline`；自動收集 `src/*.c` 編成 `obj/*.o` 後連結成 `fwsh`。 |
| docs | `docs/*.png` | demo 截圖；本報告未依圖片內容推導行為。 |
| README/report | `README_fwsh.md`、`report_fwsh.md` | 說明性文件，僅作低優先級佐證。 |

#### Module / Component Relationship

```text
main.c
  -> shell_init()
  -> shell_run()
  -> shell_cleanup()

shell.c
  -> readline(prompt)
  -> add_history(trimmed)
  -> parse_line(trimmed, &pipeline)
  -> execute_pipeline(&pipeline)
  -> free_pipeline(&pipeline)

parser.c
  -> produces Pipeline:
       Pipeline.ncmds
       Pipeline.background
       Pipeline.cmds[i].argv / argc / in_file / out_file / out_append

executor.c
  -> if single foreground builtin:
       exec_builtin(cmd) in shell process
  -> otherwise:
       pipe()
       fork()
       child: dup2() + close() + exec_builtin() or execvp()
       parent: close pipe fds + waitpid() or print background PIDs

builtin.c
  -> is_builtin(name)
  -> exec_builtin(cmd)
  -> builtins[] dispatch table
```

---

### 2. Semantic Element Extraction

#### # Direct Observation

以下只列目前實際存在的元素。

| 類型 | 名稱 | 定義位置 | 說明 |
|---|---|---|---|
| API | `shell_init` | `src/shell.c:62` / `include/shell.h:78` | 初始化 signal handlers、設定 `g_shell.running = 1`、印 banner、設定 Readline tab completion。 |
| API | `shell_run` | `src/shell.c:129` / `include/shell.h:79` | 主 REPL loop，讀取輸入、管理 history、解析 pipeline、執行 pipeline、釋放 pipeline。 |
| API | `shell_cleanup` | `src/shell.c:174` / `include/shell.h:80` | 釋放 `g_shell.history[]` 與呼叫 `rl_clear_history()`。 |
| API | `parse_line` | `src/parser.c:122` / `include/parser.h:28` | 將一行輸入解析成 `Pipeline`。 |
| API | `free_pipeline` | `src/parser.c:224` / `include/parser.h:36` | 釋放 `Pipeline` 內由 `strdup()` 配置的字串。 |
| API | `execute_pipeline` | `src/executor.c:106` / `include/executor.h:30` | 執行 `Pipeline`，處理 builtins、pipe/fork/exec/wait/background。 |
| API | `is_builtin` | `src/builtin.c:82` / `include/builtin.h:24` | 查詢 command name 是否存在於 `builtins[]`。 |
| API | `exec_builtin` | `src/builtin.c:88` / `include/builtin.h:30` | 透過 `builtins[]` dispatch table 呼叫 built-in function pointer。 |
| macro | `_POSIX_C_SOURCE 200809L` | `include/shell.h:14` | 啟用 POSIX API feature visibility，例如 `strdup`。 |
| macro | `FWSH_VERSION` | `include/shell.h:29` | banner/help 使用的版本字串。 |
| macro | `MAX_INPUT` | `include/shell.h:37` | parser `wordbuf` 大小上限，2048。 |
| macro | `MAX_ARGS` | `include/shell.h:38` | 單一 `Cmd.argv` 最大元素數，128。 |
| macro | `MAX_HISTORY` | `include/shell.h:39` | `ShellState.history` 環形緩衝區大小，50。 |
| macro | `MAX_PIPES` | `include/shell.h:40` | `Pipeline.cmds[]`、executor `pipes/pids` array 大小，16。 |
| macro | `COLOR_*` | `include/shell.h:30-34` | ANSI color escape strings。 |
| struct | `Cmd` | `include/shell.h:48-54` | 單一 command，含 `argv`、`argc`、input/output redirection。 |
| struct | `Pipeline` | `include/shell.h:57-61` | 多段 command 與 background flag。 |
| struct | `ShellState` | `include/shell.h:68-73` | shell runtime state：history ring、count/head、running flag。 |
| global state | `ShellState g_shell` | `src/shell.c:25` | shell-wide state，供 shell loop 與 builtins 使用。 |
| internal struct | `Lexer` | `src/parser.c:42-45` | parser 內部 cursor，含 input pointer 與 byte position。 |
| callback / signal handler | `sigchld_handler` | `src/shell.c:36` | `SIGCHLD` handler，使用 `waitpid(-1, NULL, WNOHANG)` 回收 child。 |
| callback / signal handler | `sigint_handler` | `src/shell.c:52` | `SIGINT` handler，呼叫 `write` 與 Readline APIs 清 line/redisplay。 |
| function pointer | `BuiltinEntry.func` | `src/builtin.c:48-52` | 指向 `int (*func)(Cmd*)`，由 dispatch table 使用。 |
| dispatch table | `builtins[]` | `src/builtin.c:54-77` | name/function/description 三欄 command table，最後 `{NULL, NULL, NULL}` sentinel。 |
| memory management | `strdup(trimmed)` | `src/shell.c:157` | 將輸入命令存入 `g_shell.history`。 |
| memory management | `strdup(wordbuf)` | `src/parser.c:177`、`194`、`205` | 為 redirection filename 與 argv 配置 heap 字串。 |
| memory management | `free_pipeline` | `src/parser.c:224-241` | 釋放 `argv[]`、`in_file`、`out_file`。 |
| memory management | `free(g_shell.history[i])` | `src/shell.c:176-179` | shell cleanup 時釋放 history ring。 |
| execution model | `fork` / `execvp` | `src/executor.c:152`、`97` | 外部命令與 pipeline 以 child process 執行。 |
| communication mechanism | `pipe` | `src/executor.c:136` | 建立 pipeline command 間的 fd pair。 |
| communication mechanism | `dup2` | `src/executor.c:66`、`78`、`173`、`176` | 把檔案或 pipe fd 接到 stdin/stdout。 |
| external interface | interactive terminal stdin/stdout | `src/shell.c:135` | `readline()` 取得使用者輸入，stdout/stderr 顯示結果與錯誤。 |
| external interface | filesystem | `src/executor.c:61`、`73`；`src/builtin.c:256`、`351` | redirection、hexdump、crc32、memmap 開檔讀寫。 |
| build target | `all` / `run` / `debug` / `valgrind` | `Makefile:59`、`97`、`101`、`108` | build/run/debug/valgrind workflow。 |

#### 目前程式碼中未觀察到

- AST tree 或 token array；parser 直接填 `Pipeline`。
- quote expansion、environment variable expansion、glob expansion、command substitution。
- job table、`fg` / `bg` / process group / terminal control。
- pipeline 中每個 child 的 structured status array；只回傳最後 forked child 的 exit status。
- thread、mutex、condition variable、atomic API。
- network/socket IPC。
- script 檔執行模式；目前可驗證入口是 interactive REPL。

---

### 3. API / Macro Inventory

#### Initialization

| 名稱 | 類型 | 定義位置 | 呼叫位置 | 呼叫來源 | 用途 | 關聯 struct / data | 對 execution flow 的影響 |
|---|---|---|---|---|---|---|---|
| `main` | entry function | `src/main.c:12` | process start | OS runtime | 呼叫 shell lifecycle 三階段 | `shell_init/run/cleanup` | 決定程式總體生命週期。 |
| `shell_init` | function | `src/shell.c:62` | `src/main.c:13` | startup | 設定 `SIGCHLD`、`SIGINT`、`SIGTSTP`；設定 running；印 banner；綁 tab completion | `g_shell.running`、Readline | 讓 REPL 可開始運作並具備 signal 行為。 |
| `sigemptyset` | POSIX API | call at `src/shell.c:65` | `shell_init` | startup | 初始化 `sa.sa_mask` | `struct sigaction sa` | signal handler mask setup。 |
| `sigaction` | POSIX API | `src/shell.c:69`、`72`、`76` | `shell_init` | startup | 註冊 signal handlers / ignore SIGTSTP | `sigchld_handler`、`sigint_handler`、`SIG_IGN` | 建立 async event dispatch path。 |
| `rl_bind_key` | Readline API | call at `src/shell.c:90` | `shell_init` | startup | 將 tab 綁到 `rl_complete` | Readline state | 啟用 tab completion。 |

#### Registration

| 名稱 | 類型 | 定義位置 | 呼叫位置 | 呼叫來源 | 用途 | 關聯 struct / data | 對 execution flow 的影響 |
|---|---|---|---|---|---|---|---|
| `builtins[]` | dispatch table | `src/builtin.c:54` | `is_builtin`、`exec_builtin`、`builtin_help` | runtime command dispatch | 註冊內建命令名稱、function pointer、描述 | `BuiltinEntry` | 新增內建命令只需加 table entry 與 function。 |
| `BuiltinEntry.func` | function pointer | `src/builtin.c:50` | `exec_builtin` | runtime command dispatch | 指向 `builtin_cd` 等函式 | `Cmd*` | indirect call path 的核心。 |
| `SRCS` / `OBJS` | Makefile variables | `Makefile:43-44` | `make all` | build | 自動收集 source 並轉成 object list | `src/*.c`、`obj/*.o` | 決定哪些 source 會被編進 binary。 |
| `-include $(OBJS:.o=.d)` | dependency include | `Makefile:82` | make | build | 引入 header dependency files | `.d` files | header 變更可觸發重編。 |

#### Execution Path

| 名稱 | 類型 | 定義位置 | 呼叫位置 | 呼叫來源 | 用途 | 關聯 struct / data | 對 execution flow 的影響 |
|---|---|---|---|---|---|---|---|
| `shell_run` | function | `src/shell.c:129` | `src/main.c:14` | startup | REPL loop | `g_shell.running`、`Pipeline` | Shell 的主要 runtime loop。 |
| `build_prompt` | helper | `src/shell.c:104` | `shell_run` | 每次 loop | 產生含 user/host/cwd 的 prompt | env `HOME` / `USER`、cwd、hostname | 影響 Readline 顯示，不改 parser/executor state。 |
| `readline` | external API | call at `src/shell.c:135` | `shell_run` | runtime input | 讀取一行互動輸入 | heap `line` | 回傳的 line 由 caller `free(line)`。 |
| `parse_line` | parser API | `src/parser.c:122` | `src/shell.c:165` | runtime | 將 trimmed line 填入 `Pipeline` | `Pipeline`、`Cmd`、`Lexer` | 成功才執行 pipeline。 |
| `execute_pipeline` | executor API | `src/executor.c:106` | `src/shell.c:165` | runtime | 執行 parsed pipeline | `Pipeline`、`Cmd` | 觸發 builtin 或 fork/exec。 |
| `free_pipeline` | cleanup API | `src/parser.c:224` | `src/shell.c:167` | runtime loop end | 釋放 parse 時 strdup 的字串 | `Pipeline` | 每次 command 後釋放 transient parse resource。 |

#### Lifecycle

| 名稱 | 類型 | 定義位置 | 呼叫位置 | 呼叫來源 | 用途 | 關聯 struct / data | 對 execution flow 的影響 |
|---|---|---|---|---|---|---|---|
| `g_shell.running` | state flag | `include/shell.h:72` / `src/shell.c:25` | `shell_init`、`shell_run`、`builtin_exit` | startup/runtime | 控制 REPL loop 是否繼續 | `ShellState` | `exit`/`quit` 將其設 0，使 `shell_run` 返回。 |
| `builtin_exit` | builtin function | `src/builtin.c:160` | `exec_builtin` | user command | 設定 `g_shell.running = 0` | `ShellState` | 結束 REPL，回到 main cleanup。 |
| `shell_cleanup` | cleanup function | `src/shell.c:174` | `src/main.c:15` | process exit | 釋放 shell-wide history resource | `g_shell.history`、Readline history | 完成 process cleanup。 |

#### Memory Handling

| 名稱 | 類型 | 定義位置 | 呼叫位置 | 呼叫來源 | 用途 | 關聯 struct / data | 對 execution flow 的影響 |
|---|---|---|---|---|---|---|---|
| `strdup(trimmed)` | allocation API | call at `src/shell.c:157` | `shell_run` | after input | 複製 command 到 history ring | `g_shell.history[idx]` | 先 free 舊 slot，再保存新字串。 |
| `strdup(wordbuf)` for `in_file` | allocation API | call at `src/parser.c:177` | `parse_line` | parse `< file` | 保存 input redirection filename | `Cmd.in_file` | executor 後續 open/dup2 使用。 |
| `strdup(wordbuf)` for `out_file` | allocation API | call at `src/parser.c:194` | `parse_line` | parse `>` / `>>` | 保存 output redirection filename | `Cmd.out_file`、`out_append` | executor 後續 open/dup2 使用。 |
| `strdup(wordbuf)` for argv | allocation API | call at `src/parser.c:205` | `parse_line` | parse command word | 保存 argv entry | `Cmd.argv[]`、`argc` | execvp/builtin 使用。 |
| `free_pipeline` | cleanup API | `src/parser.c:224` | `shell_run` | after execution or parse attempt | 釋放 `argv`、`in_file`、`out_file` | `Pipeline` | 避免每次 command parse allocation 持續累積。 |
| `rl_clear_history` | Readline cleanup API | call at `src/shell.c:181` | `shell_cleanup` | process shutdown | 清除 Readline 內部 history | Readline global state | 配合 shell 自己的 history cleanup。 |

#### Synchronization

| 名稱 | 類型 | 定義位置 | 呼叫位置 | 呼叫來源 | 用途 | 關聯 struct / data | 對 execution flow 的影響 |
|---|---|---|---|---|---|---|---|
| `sigchld_handler` | async signal callback | `src/shell.c:36` | kernel signal delivery | child exit | 非阻塞回收 child | `waitpid(-1, WNOHANG)` | 主要服務 background child；也可能影響 foreground wait，見風險分析。 |
| `sigint_handler` | async signal callback | `src/shell.c:52` | kernel signal delivery | Ctrl+C | 印 newline、操作 Readline line buffer | Readline state | Shell 本身不因 Ctrl+C 結束。 |

目前程式碼中未觀察到 pthread mutex、semaphore、condition variable 或 atomic。主要同步/事件處理來自 signal 與 `waitpid`。

#### Event Dispatch

| 名稱 | 類型 | 定義位置 | 呼叫位置 | 呼叫來源 | 用途 | 關聯 struct / data | 對 execution flow 的影響 |
|---|---|---|---|---|---|---|---|
| `is_builtin` | dispatch lookup | `src/builtin.c:82` | `execute_pipeline`、child path | runtime | 判斷 command name 是否在 `builtins[]` | `builtins[]` | 決定走 builtin 或 external。 |
| `exec_builtin` | dispatch executor | `src/builtin.c:88` | `execute_pipeline` | runtime | 透過 function pointer 執行 builtin | `BuiltinEntry.func` | 單一前景 builtin 在 shell process 執行；pipeline/background builtin 在 child 執行。 |
| `setup_redirections` | helper | `src/executor.c:58` | `exec_external` | child before execvp | 設定 `<`、`>`、`>>` | `Cmd.in_file/out_file/out_append` | 失敗時 child `_exit(1)`。 |
| `exec_external` | helper | `src/executor.c:87` | child path | runtime | 設定 redirection 後 `execvp` | `Cmd.argv` | 成功不返回；失敗 `_exit(127)`。 |

#### Logging / Debug

| 名稱 | 類型 | 定義位置 | 呼叫位置 | 呼叫來源 | 用途 | 對 execution flow 的影響 |
|---|---|---|---|---|---|---|
| `fprintf(stderr, ...)` | error output | 多處 | parser/executor/builtin | runtime errors | 顯示缺檔名、pipe/fork/open/exec/builtin 錯誤 | 通常伴隨 return non-zero 或 `_exit`。 |
| `perror` | error output | `src/executor.c:137`、`155`；`src/builtin.c:150` | pipe/fork/pwd errors | runtime errors | 依 `errno` 顯示錯誤 | pipe failure return -1；fork failure break；pwd 仍 return 0。 |
| `printf` | normal output | 多處 | banner/help/history/background/builtin results | runtime | 顯示 shell output | 對部分 lifecycle 有提示，例如 background PIDs、exit message。 |
| `Makefile debug` | build target | `Makefile:101-104` | `make debug` | developer workflow | 加入 AddressSanitizer/UBSan flags | 用於偵錯，不是 runtime code path。 |
| `Makefile valgrind` | build target | `Makefile:108-110` | `make valgrind` | developer workflow | 以 Valgrind 檢查記憶體 | 用於偵錯，不是 runtime code path。 |

#### Error Handling

| 名稱 | 類型 | 定義位置 | 呼叫位置 | 呼叫來源 | 用途 | 對 execution flow 的影響 |
|---|---|---|---|---|---|---|
| parser return `-1` | error code | `src/parser.c:154`、`174`、`191`、`203` | `parse_line` | parse errors | too many pipes、缺 filename、too many args | `shell_run` 不呼叫 `execute_pipeline`，但仍呼叫 `free_pipeline`。 |
| `setup_redirections` return `-1` | error code | `src/executor.c:64`、`76` | child path | file open failure | redirection open 失敗 | `exec_external` `_exit(1)`。 |
| `pipe` failure | error path | `src/executor.c:136-143` | `execute_pipeline` | runtime | 關閉已建立 pipes 後 return -1 | 不 fork。 |
| `fork` failure | error path | `src/executor.c:152-156` | `execute_pipeline` | runtime | `perror` 後 break | 已 fork 的 child 仍會被後續 parent wait。 |
| `execvp` failure | error path | `src/executor.c:97-101` | child | runtime | 顯示錯誤並 `_exit(127)` | parent wait 後取得 127。 |
| builtin return non-zero | status | builtin functions | `exec_builtin` | runtime | command-specific failure | 單一 foreground builtin 直接成為 `execute_pipeline` return；child builtin 用 `_exit(status)`。 |

#### Cleanup

| 名稱 | 類型 | 定義位置 | 呼叫位置 | 呼叫來源 | 用途 | 關聯 struct / data | 對 execution flow 的影響 |
|---|---|---|---|---|---|---|---|
| `free_pipeline` | cleanup | `src/parser.c:224` | `src/shell.c:167` | every REPL iteration after parse attempt | 釋放 command-level heap strings | `Pipeline` | 每次輸入後清理 transient state。 |
| `free(line)` | cleanup | `src/shell.c:150`、`168` | `shell_run` | empty / processed input | 釋放 Readline 回傳 line | `char* line` | 避免 Readline line leak。 |
| `free(g_shell.history[idx])` | cleanup-before-overwrite | `src/shell.c:156` | `shell_run` | history ring overwrite | 釋放被覆蓋的舊 history | `ShellState.history` | 保持 history ring bounded。 |
| `shell_cleanup` | cleanup | `src/shell.c:174` | `main` | program shutdown | 釋放全部 shell history 與 Readline history | `ShellState`、Readline | process exit 前釋放 global resources。 |
| `close` | fd cleanup | `src/executor.c:67`、`79`、`140-141`、`180-181`、`205-206` | redirection/pipe paths | child/parent runtime | 關閉已 dup 或不再需要的 fd | pipe fd / redirection fd | 防止 fd leak 與 pipe EOF 延遲。 |
| `fclose` | FILE cleanup | `src/builtin.c:304`、`379`、`439` | hexdump/crc32/memmap | builtin runtime | 關閉 `FILE*` | `fp` | 釋放 stdio resource。 |

---

### 4. Call Graph

#### Initialization Chain

```text
OS starts ./fwsh
  -> main()
  -> shell_init()
       -> sigemptyset(&sa.sa_mask)
       -> sa.sa_flags = SA_RESTART
       -> sigaction(SIGCHLD, sigchld_handler)
       -> sigaction(SIGINT, sigint_handler)
       -> sigaction(SIGTSTP, SIG_IGN)
       -> g_shell.running = 1
       -> print banner
       -> rl_bind_key('\t', rl_complete)
  -> shell_run()
```

#### Runtime Chain: REPL

```text
shell_run()
  while (g_shell.running)
    -> build_prompt(prompt)
    -> line = readline(prompt)
    -> if line == NULL: print exit and break
    -> trim leading whitespace
    -> if empty: free(line), continue
    -> add_history(trimmed)
    -> idx = g_shell.hist_head % MAX_HISTORY
    -> free(g_shell.history[idx])
    -> g_shell.history[idx] = strdup(trimmed)
    -> update hist_head/hist_count
    -> Pipeline pipeline = zeroed stack object
    -> parse_line(trimmed, &pipeline)
       -> if 0: execute_pipeline(&pipeline)
    -> free_pipeline(&pipeline)
    -> free(line)
```

#### Runtime Chain: Parser

```text
parse_line(line, pipeline)
  -> Lexer lex = { input=line, pos=0 }
  -> memset(pipeline, 0)
  -> cur = &pipeline->cmds[0]
  -> loop:
       skip_whitespace
       c = input[pos]
       '\0' or '\n' -> finish
       '|' -> cmd_idx++, bounds check, switch cur
       '&' -> pipeline->background = 1
       '<' -> read filename word, cur->in_file = strdup(wordbuf)
       '>' -> optional second '>' sets out_append, read filename, cur->out_file = strdup(wordbuf)
       word -> read_word, cur->argv[cur->argc++] = strdup(wordbuf), argv NULL terminate
  -> pipeline->ncmds = cmd_idx + 1
  -> trim trailing empty command if input ends after pipe
  -> return 0
```

#### Runtime Chain: Executor

```text
execute_pipeline(pipeline)
  -> if ncmds == 0: return 0
  -> if single foreground command and builtin:
       return exec_builtin(cmd)

  -> npipes = ncmds - 1
  -> create all pipes
  -> for each Cmd:
       if argc == 0: continue
       pid = fork()
       child:
         if i > 0: dup2(previous pipe read end, STDIN_FILENO)
         if i < npipes: dup2(current pipe write end, STDOUT_FILENO)
         close all pipe fds
         if builtin: _exit(exec_builtin(cmd))
         exec_external(cmd)
           -> setup_redirections(cmd)
           -> execvp(argv[0], argv)
           -> on failure: _exit(127)
       parent:
         store pid
  -> parent close all pipe fds
  -> if foreground:
       waitpid(each pid)
       return last command exit status
     else:
       print [background] pids
       return 0
```

#### Cleanup Chain

```text
per-command cleanup:
  shell_run
    -> free_pipeline(&pipeline)
       -> free argv[j]
       -> free in_file/out_file
       -> set pointers NULL and argc=0
    -> free(line)

program cleanup:
  shell_run returns
  -> main
  -> shell_cleanup
       -> for every history slot:
            free(g_shell.history[i])
            history[i] = NULL
       -> rl_clear_history()
  -> return 0
```

#### Callback Chain

```text
Signal callback:
  SIGCHLD -> sigchld_handler -> waitpid(-1, NULL, WNOHANG) loop
  SIGINT  -> sigint_handler  -> write newline + rl_on_new_line + rl_replace_line + rl_redisplay
  SIGTSTP -> SIG_IGN

Builtin dispatch callback:
  execute_pipeline / child path
    -> is_builtin(argv[0])
    -> exec_builtin(cmd)
       -> builtins[i].func(cmd)
          -> builtin_cd / builtin_pwd / builtin_exit / ...
```

#### Indirect Call Chain / Dispatch Table

| Dispatch point | Table / function pointer | Target | Evidence |
|---|---|---|---|
| built-in command lookup | `builtins[]` | command name -> function pointer | `src/builtin.c:54-77` |
| function pointer call | `builtins[i].func(cmd)` | `builtin_cd` / `builtin_pwd` / etc. | `src/builtin.c:91` |
| signal delivery | `sigaction(..., sa.sa_handler = ...)` | `sigchld_handler` / `sigint_handler` / `SIG_IGN` | `src/shell.c:68-76` |
| Readline key binding | `rl_bind_key('\t', rl_complete)` | Readline completion callback | `src/shell.c:90` |
| external command replacement | `execvp(cmd->argv[0], cmd->argv)` | PATH-resolved program | `src/executor.c:97` |

---

### 5. Struct / Resource Tracing

#### `Cmd`

##### # Direct Observation

Defined at `include/shell.h:48-54`:

| 欄位 | allocation / init | 使用位置 | ownership / lifetime |
|---|---|---|---|
| `argv[MAX_ARGS]` | `parse_line` 以 `strdup(wordbuf)` 填入；`Pipeline` 初始 `memset` 為 0 | `is_builtin`、`exec_builtin`、`execvp` | `Pipeline` 擁有 argv 字串；`free_pipeline` 釋放。 |
| `argc` | `parse_line` 遞增 | parser bounds check、executor skip empty command、builtins argument handling | value state，跟 `Pipeline` stack object 同生命週期。 |
| `in_file` | `<` 後 `strdup(wordbuf)` | `setup_redirections` `open(..., O_RDONLY)` | `Pipeline` 擁有；`free_pipeline` 釋放。 |
| `out_file` | `>` / `>>` 後 `strdup(wordbuf)` | `setup_redirections` `open(..., flags, 0644)` | `Pipeline` 擁有；`free_pipeline` 釋放。 |
| `out_append` | parser 看到 `>>` 時設 1 | `setup_redirections` 決定 `O_APPEND` 或 `O_TRUNC` | value state。 |

#### `Pipeline`

Defined at `include/shell.h:57-61`:

| 欄位 | allocation / init | 使用位置 | ownership / lifetime |
|---|---|---|---|
| `cmds[MAX_PIPES]` | `shell_run` stack object，`memset` zero；parser 填入 | parser/executor/free_pipeline | struct 本身在 stack；內部字串由 `strdup` 配置。 |
| `ncmds` | parser 結尾設定 | executor loops、free_pipeline loops | 決定 executor fork 數與 cleanup 範圍。 |
| `background` | parser 看到 `&` 設 1 | executor 決定 wait 或 print background PIDs | 決定 parent 是否 `waitpid`。 |

#### `ShellState`

Defined at `include/shell.h:68-73`; global instance at `src/shell.c:25`:

| 欄位 | allocation / init | 使用位置 | ownership / lifetime |
|---|---|---|---|
| `history[MAX_HISTORY]` | static zero-init；每次 command 用 `strdup(trimmed)` 寫入 | `builtin_history`、`shell_cleanup` | `g_shell` 擁有 history 字串；overwrite 前 free，exit 時全部 free。 |
| `hist_count` | global initializer 0；shell_run 更新 | `builtin_history` | value state，最多 50。 |
| `hist_head` | global initializer 0；shell_run 更新 | history ring write/read | value state，持續遞增。 |
| `running` | `shell_init` 設 1；`builtin_exit` 設 0 | `shell_run` while condition | 控制 REPL lifecycle。 |

#### Resource Tracing

| Resource | allocation / init | owner | release timing |
|---|---|---|---|
| Readline `line` | `readline(prompt)` | `shell_run` | empty input 或 iteration 結尾 `free(line)`。 |
| Pipeline strings | `parse_line` 中 `strdup` | `Pipeline` / caller `shell_run` | `free_pipeline`。 |
| Shell history strings | `strdup(trimmed)` | `g_shell.history[]` | overwrite 前 free；`shell_cleanup` 全部 free。 |
| Pipe fds | `pipe(pipes[i])` | parent creates; children inherit after fork | child closes all after dup2；parent closes all after fork loop；partial pipe failure closes already-created fds。 |
| Redirection fds | `open(in_file/out_file)` | child process | after `dup2`, child `close(fd)`。 |
| `FILE*` in builtins | `fopen` | builtin function | `fclose` before return on success path。 |
| CRC table | static `crc32_table[256]` | process global | no release needed; built once and reused. |

#### State Transition

```text
ShellState:
  zero-initialized
  -> shell_init: running=1
  -> shell_run: history slots filled/overwritten
  -> builtin_exit: running=0
  -> shell_cleanup: history freed, Readline history cleared

Pipeline:
  zeroed stack object
  -> parse_line fills cmds/background/ncmds
  -> execute_pipeline consumes by reference
  -> free_pipeline frees internal heap strings and resets ncmds=0

Child process:
  forked
  -> dup2 pipe fds as needed
  -> close inherited pipe fds
  -> builtin in child or exec_external
  -> _exit(status) or execvp replacement
```

#### Data Passing Path

```text
terminal input
  -> readline returns heap line
  -> trimmed pointer into line
  -> add_history(trimmed)
  -> g_shell.history[idx] = strdup(trimmed)
  -> parse_line(trimmed, &pipeline)
  -> Cmd.argv / in_file / out_file = strdup(wordbuf)
  -> execute_pipeline(&pipeline)
     -> builtin receives Cmd*
     -> external receives argv via execvp
     -> redirection receives filenames via open
  -> free_pipeline(&pipeline)
  -> free(line)
```

#### Callback Binding

- Signal callbacks are bound in `shell_init` through `sigaction`.
- Builtin callbacks are bound statically in `builtins[]`.
- Readline tab completion is bound via `rl_bind_key('\t', rl_complete)`.

---

### 6. Execution Trace

#### Initialization Flow

```text
make all
  -> compile src/*.c to obj/*.o
  -> link fwsh with -lreadline

./fwsh
  -> main
  -> shell_init
       -> setup SIGCHLD/SIGINT/SIGTSTP
       -> set running
       -> print banner
       -> enable Readline tab completion
  -> shell_run
```

#### Runtime Flow

```text
User types command
  -> readline
  -> trim
  -> history update
  -> Pipeline zero-init
  -> parse_line
  -> execute_pipeline
  -> free_pipeline
  -> free line
  -> next prompt unless g_shell.running == 0
```

#### Cleanup Flow

```text
exit command
  -> execute_pipeline single foreground builtin
  -> exec_builtin
  -> builtin_exit
  -> g_shell.running = 0
  -> shell_run loop ends
  -> shell_cleanup
  -> main returns
```

#### Data Flow

```text
"cat input | grep x > out"
  -> parser:
       cmd[0].argv = ["cat", "input", NULL]
       cmd[1].argv = ["grep", "x", NULL]
       cmd[1].out_file = "out"
       cmd[1].out_append = 0
       ncmds = 2
  -> executor:
       pipe[0]
       child 0 stdout -> pipe[0][1] -> execvp("cat", ...)
       child 1 stdin  -> pipe[0][0], stdout -> open("out") -> execvp("grep", ...)
       parent closes pipe and waits
```

#### Event Flow

```text
SIGINT
  -> sigint_handler
  -> newline + Readline redisplay

SIGCHLD
  -> sigchld_handler
  -> waitpid(-1, WNOHANG) until no more exited child

User & command
  -> parse_line sets background=1
  -> executor does not wait
  -> prints child PIDs
  -> later SIGCHLD handler reaps exited child
```

#### Ownership Transfer

目前程式碼中沒有複雜 ownership transfer。可驗證的關係是：

- `readline()` 回傳的 `line` ownership 交給 `shell_run`，由 `free(line)` 釋放。
- `parse_line()` 以 `strdup()` 建立的字串 ownership 交給 `Pipeline`，由 `free_pipeline()` 釋放。
- `execvp()` 成功後 child process image 被替換；原本在 child address space 的 heap 不再需要由 shell code 釋放。
- Pipe fd 在 `fork()` 後被 parent/child 各自關閉；這是 fd lifecycle，不是 heap ownership。

---

## 第二階段：Architecture / API Technical Report

### 1. Entry Point 行為

#### # Direct Observation

`fwsh` 的 entry point 是 `src/main.c:12` 的 `main()`。它沒有解析 argv，也沒有 batch/script mode。可驗證的流程只有：

```text
shell_init()
shell_run()
shell_cleanup()
return 0
```

這表示目前 `fwsh` 是互動式 shell。`Makefile:97-98` 的 `run` target 也是直接執行 `./fwsh`。

---

### 2. Callback Registration Chain

#### Signal Callback

`shell_init` 使用同一個 `struct sigaction sa` 依序設定：

- `SIGCHLD -> sigchld_handler`
- `SIGINT -> sigint_handler`
- `SIGTSTP -> SIG_IGN`

`sa.sa_flags = SA_RESTART`，表示某些被 signal 中斷的 syscall 可能被自動 restart。此處是直接 code observation；Readline 本身如何受影響需依 library 行為，無法只從本 code 完整確認。

#### Builtin Dispatch Callback

`builtin.c` 的 `BuiltinEntry` 內含 function pointer：

```c
int (*func)(Cmd*);
```

`exec_builtin` 逐筆比對 `builtins[i].name`，命中後呼叫 `builtins[i].func(cmd)`。目前 table 註冊的名稱是：`cd`、`pwd`、`exit`、`quit`、`help`、`history`、`clear`、`hexdump`、`crc32`、`memmap`。

#### Readline Callback

`shell_init` 呼叫 `rl_bind_key('\t', rl_complete)`，把 Tab key 綁到 Readline 內建 completion function。此專案目前沒有自訂 completion function。

---

### 3. Runtime Dispatch Flow

#### Single Foreground Builtin

`execute_pipeline` 特別處理「只有一個 command 且不是 background」的 builtin：

```text
ncmds == 1 && !background
  -> if argc == 0 return 0
  -> if is_builtin(argv[0]) return exec_builtin(cmd)
```

這讓 `cd`、`exit` 這類需要改變 shell process state 的 command 在 parent shell 內執行，而不是 fork 後在 child 內執行。

#### Pipeline / Background / External

只要是 pipeline、background，或不是 builtin，就進入 fork path。child 中仍會檢查 builtin；若是 builtin，child 直接 `_exit(exec_builtin(cmd))`。這代表 pipeline 中的 builtin 不會改變 parent shell 的 `cwd` 或 `running`。

#### Redirection

目前 redirection 只在 `exec_external()` 內呼叫 `setup_redirections()`。因此：

- 外部命令支援 `<`、`>`、`>>`。
- pipeline/background 中的 builtin 在 child path 直接 `exec_builtin(cmd)`，不會呼叫 `setup_redirections()`。
- single foreground builtin 也不會呼叫 `setup_redirections()`。

所以「builtin redirection」目前程式碼中未觀察到實作。若執行 `pwd > out`，parser 會記錄 `out_file`，但 single foreground builtin path 直接 `exec_builtin`，不會處理 `out_file`。這是可直接從 `execute_pipeline` 與 `exec_external` 的呼叫關係驗證的行為。

---

### 4. Indirect Call Path

#### # Direct Observation

本專案的 indirect dispatch 有三類：

1. signal delivery 透過 `sigaction` 呼叫 handler。
2. builtin command 透過 `builtins[]` function pointer 呼叫。
3. external command 透過 `execvp` 將 child process 替換成 PATH 中的程式。

目前程式碼中未觀察到 vtable、plugin registry、dynamic loading、dlopen 或 script-defined callback。

---

### 5. Resource Lifecycle

#### Heap Resource

`shell_run` 每輪都會建立 stack `Pipeline`，parser 將字串複製到 heap，executor 只讀取，最後 `free_pipeline` 釋放。Readline line buffer 由 `free(line)` 釋放。history ring 則跨 command 保存，直到被覆蓋或 `shell_cleanup`。

#### File Descriptor Resource

Executor 對 fd lifecycle 有明確處理：

- pipe 建立失敗時，關閉已建立的 pipe fd。
- child `dup2` 後關閉所有 pipe fd。
- parent fork 完所有 child 後關閉所有 pipe fd。
- redirection `open` 後 `dup2`，再 `close(fd)`。

目前程式碼中未檢查 `dup2` return value；這是 error handling gap。

#### Process Resource

Foreground command 使用 parent `waitpid(pids[i], &status, 0)` 等待。Background command 不 wait，僅印 PID，後續由 `SIGCHLD` handler 用 `waitpid(-1, WNOHANG)` 回收。

---

### 6. Error Propagation Path

#### Parser Error

| 錯誤點 | 回傳 | 後續 |
|---|---|---|
| pipe 段數超過 `MAX_PIPES` | `-1` | `shell_run` 不呼叫 executor，仍呼叫 `free_pipeline`。 |
| `<` 後沒有 filename | `-1` | 同上。 |
| `>` / `>>` 後沒有 filename | `-1` | 同上。 |
| argv 超過 `MAX_ARGS - 1` | `-1` | 同上。 |

#### Executor Error

| 錯誤點 | 行為 |
|---|---|
| `pipe()` 失敗 | `perror`，關閉已建立 pipes，return `-1`。 |
| `fork()` 失敗 | `perror`，break fork loop；parent 關閉 pipes，等待已 fork 的 child。 |
| redirection `open()` 失敗 | child 印錯，`setup_redirections` return `-1`，`exec_external` `_exit(1)`。 |
| `execvp()` 失敗 | child 印錯，`_exit(127)`。 |
| foreground wait | 回傳最後一個 forked child 的 exit status；若 child 非正常 exit，設 `-1`。 |

#### Builtin Error

- `cd`：`HOME` / `OLDPWD` 缺失或 `chdir` 失敗時 return 1。
- `hexdump`：缺參數、length invalid、`fopen` 失敗 return 1。
- `crc32`：缺參數、`fopen` 失敗 return 1。
- `memmap`：`/proc/iomem` 開啟失敗 return 1。
- `exec_builtin` 沒找到 command 時 return `-1`，但正常呼叫前已由 `is_builtin` 或 table lookup 保護。

---

### 7. 比較分析

#### Single foreground builtin vs child builtin

| 情境 | 執行位置 | 影響 parent shell state |
|---|---|---|
| `cd /tmp` | parent shell process | 會改變 shell cwd。 |
| `cd /tmp | pwd` | child process | 不會改變 parent shell cwd。 |
| `exit` | parent shell process | 會設 `g_shell.running = 0`。 |
| `exit | cat` 或 `exit &` | child process | 只會讓 child exit，不會直接停止 parent shell。 |

差異原因可由 code 驗證：只有 `ncmds == 1 && !background` 的 builtin 走 direct `exec_builtin` path；其他都 fork。

#### `>` vs `>>`

兩者都設定 `Cmd.out_file`，差異是 parser 看到第二個 `>` 時設 `out_append = 1`。executor 依該 flag 選擇：

- `>`：`O_WRONLY | O_CREAT | O_TRUNC`
- `>>`：`O_WRONLY | O_CREAT | O_APPEND`

#### Builtin dispatch vs external dispatch

| 類型 | 查找方式 | 執行方式 |
|---|---|---|
| builtin | `builtins[]` linear search | function pointer `func(cmd)` |
| external | PATH search by `execvp` | child process image replacement |

使用原因只能依 code 說明：builtin 需要 shell 內部 state 或直接 C 函式；external 交給 POSIX process model 與 PATH resolution。

#### Resource management model

`Pipeline` 是 stack object，但其內部字串是 heap allocation；history 是 shell-global heap strings；pipes/redirection 是 fd resource；builtins 的 `FILE*` 是 stdio resource。此專案沒有統一 allocator 或 RAII-style wrapper，cleanup 是分散在 `free_pipeline`、`shell_cleanup`、executor close path 與各 builtin function 內。

---

### 8. Debug / Risk Analysis

#### Potential Memory Leak

- 正常成功 parse path：`parse_line` 配置的 `argv/in_file/out_file` 會由 `free_pipeline` 釋放。
- 風險：`parse_line` 在某些 error return 前尚未設定 `pipeline->ncmds`。例如 too many pipes、missing filename、too many args 都可能在已 `strdup` 一些字串後 return `-1`；`shell_run` 會呼叫 `free_pipeline`，但 `free_pipeline` 以 `pipeline->ncmds` 控制 loop。若 `ncmds` 仍是 0，就不會釋放已配置的字串。這是從 `parse_line` 的 early return 與 `free_pipeline` loop 條件直接可見的 leak risk。
- `strdup(trimmed)` 與 parser 中的 `strdup(wordbuf)` 都未檢查 NULL；記憶體不足時可能產生後續 NULL dereference 或不完整 state。

#### Invalid Ownership Transfer

- `readline` 回傳的 `line` 在 `shell_run` 中被 free；`trimmed` 只是指向 `line` 內部，不被保存為 raw pointer。保存到 history 前有 `strdup`，因此正常 path 沒有 dangling `trimmed`。
- `Pipeline` 只持有 `strdup` 出來的字串，executor 不保存 pointer 到下一輪；正常 path 沒有跨 iteration dangling pointer。
- child process fork 後會複製 address space；child `_exit` 或 execvp 後不需要回到 parent cleanup。

#### Callback Misuse Risk

- `sigint_handler` 呼叫 `rl_on_new_line`、`rl_replace_line`、`rl_redisplay`。這些 Readline calls 是否 async-signal-safe 無法從本 code 驗證；可直接確認的是 handler 內除了 `write` 之外還呼叫了 Readline API。
- `sigchld_handler` 使用 `waitpid(-1, WNOHANG)`，可能回收任意已結束 child。`execute_pipeline` foreground path 也對特定 pid 呼叫 blocking `waitpid`。保守推論：若 foreground child exit 造成 SIGCHLD handler 先回收，後續 foreground `waitpid(pids[i], ...)` 可能失敗；目前程式碼沒有檢查 `waitpid` return value。

#### Lifecycle Mismatch

- Background job 沒有 job table；`execute_pipeline` 只印 PID，後續只能靠 `SIGCHLD` handler 回收，無法查詢或管理 job state。這不是 bug 本身，但和 shell 常見 job control 不同，需標示目前程式碼中未觀察到 job control。
- Parser 支援 `background` flag，但 builtins 在 background/pipeline path 會在 child process 中執行，因此 `exit &` 或 `cd &` 不會改 parent shell state。這符合目前 code path，但可能與使用者直覺不同。
- Builtin redirection 未在目前 code path 中實作，如 `help > file` 不會經過 `setup_redirections`。

#### Concurrency Issue

- 此專案沒有 threads；主要 concurrent behavior 是 parent/child processes 與 signal handler。
- `crc32_table_ready` 與 `crc32_table` 是 static global，但 fwsh 沒有 multi-threaded command execution；目前程式碼中未觀察到同 process 內 concurrent builtin 執行。
- signal handler 與 main execution flow 共享 process-level child state；如前述 `SIGCHLD` handler 與 foreground `waitpid` 可能競爭 child reaping。

#### Parse / Compile Risk

- `src/shell.c:137` 目前可見註解行以 `/*` 開頭但顯示為 `?/` 結尾，而不是標準 `*/`。
- `src/builtin.c:282` 目前可見註解行也顯示為 `?/` 結尾。
- 若檔案實際內容確實如此，會造成 C comment 未正常關閉並影響編譯。此報告未執行 build；此點僅依目前讀到的 source 標示為風險。

#### File Descriptor / Error Handling Risk

- `dup2` return value 未檢查；若 dup2 失敗，child 仍可能繼續執行 builtin 或 execvp，I/O routing 會不符合預期。
- child path 關閉 pipe fd 的邏輯完整，但若 `fork` 中途失敗，只會 break，仍會等待已 fork 的 children；未 fork 的 command 不會執行。這是合理降級，但沒有把 fork failure status 明確傳回給 caller。

---

## 補充：Command / Interface 對照

### Builtin Table

| Command | Function | 定義位置 | 直接影響 state |
|---|---|---|---|
| `cd` | `builtin_cd` | `src/builtin.c:111` | `chdir` 改變 shell process cwd；設定 `OLDPWD`。 |
| `pwd` | `builtin_pwd` | `src/builtin.c:144` | 無，讀 cwd 後輸出。 |
| `exit` / `quit` | `builtin_exit` | `src/builtin.c:160` | `g_shell.running = 0`。 |
| `help` | `builtin_help` | `src/builtin.c:170` | 無，列印 `builtins[]` descriptions。 |
| `history` | `builtin_history` | `src/builtin.c:188` | 無，讀 `g_shell.history`。 |
| `clear` | `builtin_clear` | `src/builtin.c:203` | 無，輸出 ANSI clear sequence。 |
| `hexdump` | `builtin_hexdump` | `src/builtin.c:237` | 開檔讀取並輸出 hex/ASCII。 |
| `crc32` | `builtin_crc32` | `src/builtin.c:345` | 第一次建立 CRC table；開檔計算 checksum。 |
| `memmap` | `builtin_memmap` | `src/builtin.c:405` | 讀 `/proc/iomem` 並依關鍵字上色輸出。 |

### Parser Token Behavior

| Syntax | Parser effect | Executor effect |
|---|---|---|
| `|` | 切到下一個 `Cmd` | 建立 pipe，前一段 stdout 接下一段 stdin。 |
| `&` | `pipeline->background = 1` | parent 不 wait，印 background PIDs。 |
| `< file` | `cmd->in_file = strdup(file)` | external command child open read-only, dup2 stdin。 |
| `> file` | `cmd->out_file = strdup(file)`, `out_append = 0` | external command child open with `O_TRUNC`, dup2 stdout。 |
| `>> file` | `cmd->out_file = strdup(file)`, `out_append = 1` | external command child open with `O_APPEND`, dup2 stdout。 |
| single quotes | literal copy until closing `'` | no later expansion。 |
| double quotes | copy until closing `"`, supports `\"` and `\\` | no variable expansion。 |

---

## 結論

`fwsh` 目前是一個 userspace interactive mini shell。核心 execution semantics 是：`main` 建立 shell lifecycle，`shell_run` 透過 GNU Readline 讀取一行輸入，`parser` 將文字轉成 `Pipeline`，`executor` 根據 builtin/pipeline/background 狀態決定 direct builtin 或 fork/pipe/exec，最後由 `free_pipeline` 與 `shell_cleanup` 清理 heap resources。

可驗證的 callback chain 包含 signal handlers、Readline tab completion、以及 `builtins[]` function pointer dispatch。ownership 上，`readline` line、parser `strdup` 字串、history ring、pipe fd、redirection fd、builtin `FILE*` 都有各自 cleanup path。主要風險集中在 parser early error 的 heap cleanup、signal handler 與 foreground waitpid 的 child reaping 競爭、builtin redirection 未實作、`dup2` 未檢查，以及目前 source 中疑似未正常關閉的註解行。
