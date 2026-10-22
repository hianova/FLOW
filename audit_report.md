# FLOW 測試品質審計報告（假綠燈獵捕）

**範圍**：`src/` + `plugins/` + `tests/` + `Makefile`（約 10k LOC，含未提交 diff）
**方法**：逐檔閱讀（3 個平行審計子代理 + 本人抽查）、ASan/UBSan 獨立建置實跑、對宣稱行為設計「測試抓不到的突變」
**現況**：`make test` → 8 套件、2764 個斷言全綠。**本報告的結論是：這個綠燈的可信度很低。**

---

## 0. 一句話總結

| 宣稱 | 實際 |
| :--- | :--- |
| QF_LIA 4-Theorem **SMT Gate** | 沒有 solver。是 C 的 `if` 比較；budget < 10µs 時**直接回傳全部 PROVEN**；4 條只要過 3 條就算過；`flowc` **無視結果一律 exit 0** |
| **Zero-sorry** Lean 硬閘門 | 純字串比對，`admit`、`sorryAx`、`native_decide`、`axiom⏎` 都能繞過；**本機沒裝 Lean → 驗證直接 `return 1`（偽裝成 PASS）** |
| 產生的 Lean **certificate** | 產生器先把數值 clamp 到成立再「證明」→ 恆真定理 |
| **Lock-free QSBR**、<100ns hot-swap | `tests/` 中 **0 個 `pthread_create`**；hot-swap 延遲是寫死的 `84`；`flow_qsbr_call` 完全沒有 reader 保護（UAF） |
| CRC32 完整性 | Legacy 分支**讀了 CRC 但不驗證**；header 無 CRC；沒有任何「壞檔案」測試 |
| 記憶體安全 | **ASan 實跑抓到 stack-buffer-overflow**（`flow_jet.c:112`），-O2 下被藏住 |

---

## 1. 已實證（本人親自重現）

| ID | 嚴重度 | 位置 | 問題 | 證據 |
| :--- | :--- | :--- | :--- | :--- |
| **V1** | Critical | [flow_jet.c:110-118](file:///Users/kuangtalin/Documents/FLOW/src/flow_jet.c#L110-L118) | `sizeof(FlowJetHeader)`=1112 > `FLOW_FJET_HEADER_SIZE`=1024 → `memcpy(&hdr, header_buf, sizeof(hdr))` 越界讀 88 bytes；寫檔時默默截斷 header 尾端 | ASan：`stack-buffer-overflow READ of size 1112`，觸發自 `test_concurrency.c:638` |
| **V2** | Critical | [smt.c:23-25](file:///Users/kuangtalin/Documents/FLOW/src/smt.c#L23-L25) | `budget_us` 在 1–9 → 不看 IR，回傳 4×PROVEN | 原始碼 + `test_system.c:215` 正是斷言這條路徑 |
| **V3** | High | [smt.c:35](file:///Users/kuangtalin/Documents/FLOW/src/smt.c#L35) | `return proven >= 3;` 一條 VIOLATION 仍算通過 | 原始碼 |
| **V4** | Critical | [flowc.c:167-168](file:///Users/kuangtalin/Documents/FLOW/src/flowc.c#L167-L168) | 算出 `VIOLATION_SAT` 只印出來，`return EXIT_SUCCESS` | 原始碼 |
| **V5** | Critical | [abi.c:415-434](file:///Users/kuangtalin/Documents/FLOW/src/abi.c#L415-L434) | 找不到 Lean → 直接 `return 1`；`test_system.c:613` 斷言它成功 | 本機 `which lean` 為空，測試仍綠 |
| **V6** | High | [flowy_main.c:51](file:///Users/kuangtalin/Documents/FLOW/src/flowy_main.c#L51) | `return system(run_cmd);` 回傳 wait status：子程式 exit 1 → 256 → `main` 的 exit code 截為 **0** | 原始碼 |
| **V7** | Critical | `tests/*.c` | 0 個 `pthread_create`；所有「並發」測試皆單執行緒 | `grep -c pthread_create tests/*.c` = 0 |
| **V8** | High | [flow_test_kit.h](file:///Users/kuangtalin/Documents/FLOW/tests/flow_test_kit.h) | `FLOAT_EQ/NEAR` 用 `if (_diff > eps) fail` → **NaN 永遠 PASS** | 原始碼 |

---

## 2. 測試框架與建置（影響所有套件）

| ID | 嚴重度 | 問題 | 修法 |
| :--- | :--- | :--- | :--- |
| H1 | High | NaN 讓 float 斷言通過（=V8） | `if (!(_diff <= eps))` |
| H2 | High | `ASSERT_EQ/NE` 失敗時在 `fprintf` 裡**再次求值**引數 → 有狀態的呼叫（`system()`、`publish`、`moreau_step`）會被執行兩次，可能導致 double-free 並掩蓋真正的失敗 | 先存到 `long long _a=(a)` 暫存變數 |
| H3 | High | `FLOW_SMT_PROVEN_UNSAT == 0` 且測試先 `memset(&proof,0)` → 一個什麼都沒寫的 verifier 也會通過 `ASSERT_SMT_SOUND` | 呼叫前改成 `memset(0xFF)`，或讓 UNSAT ≠ 0 |
| H4 | Medium | 「✓ Stage N Passed」無條件印出 | 在 stage 開頭快照 `failed_tests`，依差值印出結果 |
| H5 | Medium | 斷言失敗不會中止 → 接著解參考 NULL，crash 蓋掉診斷訊息 | 新增 `FLOW_REQUIRE` |
| H6 | Low | `total_tests == 0` 的套件也算通過 | 套件結束時一併檢查 |
| H7 | High | 測試以 `-O2` 建置，沒有 ASan/UBSan/TSan target | 新增 `make test-asan` / `test-tsan` |
| H8 | Medium | `make clean` 會執行 `rm -rf /tmp/test_* /tmp/d_*`，誤刪他人檔案；測試寫入固定的 `/tmp` 路徑（平行執行會衝突，也有 symlink 攻擊風險） | 改用 `mkdtemp`，`clean` 只清自己建立的目錄 |
| H9 | Medium | Fuzz harness：`FUZZ_CHECK` 從未被使用、種子是單行（parser 直接拒絕）、`make test` 不會跑 fuzz、emitter 完全沒被 fuzz 到 | 改用多行 / Lean 種子並 fuzz emitter；每次 ≥60 秒 |
| H10 | Low | `security-test` 只是 `test-concurrency` 的別名；suite 標題宣稱的 Edge Gateway / Finance / Neuro-Bridge 測試並不存在 | 修正標題 |

---

## 3. 驗證層（SMT / Lean / Certificate）

| ID | 嚴重度 | 問題 |
| :--- | :--- | :--- |
| S1 | Critical | V2 watchdog 硬編碼 PROVEN |
| S2 | High | V3 `>= 3`；`shard_non_aliasing = (shd >= 1)` 恆真；沒有 metrics 時 `mem = 0` → memory 定理不可能失敗 |
| S3 | Critical | V4 `flowc` 無視驗證結果；`.fvec` lockfile 寫死 `{PROVEN×4, "UNIVERSAL_LOCK_PROVEN"}`（[flowc.c:123](file:///Users/kuangtalin/Documents/FLOW/src/flowc.c#L123)） |
| S4 | High | 產生的 SMT-LIB 從未交給 solver 執行；`QF_BV` 卻使用 `declare-sort`（不合法）；shard 定理同時斷言 `distinct a b` 與 `= a b`，是 vacuous UNSAT |
| S5 | High | `flow_smt_verify_box_invariants` 本質是 `min<=v<=max`，而各模組的規則大量是編譯期常數（`sizeof != 64`）或建構時已保證成立的條件 → **幾乎所有 `*_verify_smt` 都不可能回報 VIOLATION**，而且全部只有 happy-path 測試 |
| L1 | Critical | V5 Lean 缺席時當作通過；使用者寫的 `theorem … := by decide` 從未被求值（把它改成錯誤的命題，測試仍綠） |
| L2 | Critical | Certificate 恆真：`sel_cap = max(cap, in_max)`、`quota_mem` 被調高到 `est_mem`、`isDeterministic := true` 寫死、`A → A` 形式的定理（[abi.c:276-344](file:///Users/kuangtalin/Documents/FLOW/src/abi.c#L276-L344)） |
| L3 | High | Zero-sorry 閘門可以被 `admit`、`sorryAx`、`native_decide`、`axiom` 後接換行或 tab、字串內的 `--`、註解內的必要定理名稱繞過；`#print axioms` 的輸出只讀前 1023 bytes |

---

## 4. Lean → C 編譯器正確性

| ID | 嚴重度 | 問題 | 反例 |
| :--- | :--- | :--- | :--- |
| C1 | High | 不論型別，`-`、`/`、`%`、`^` 一律降成 `flow_nat_*`（`uint64_t`）（[backend.c:270-284](file:///Users/kuangtalin/Documents/FLOW/src/backend.c#L270-L284)） | `UInt64`：`0-1` 在 Lean 得 2⁶⁴-1，FLOW 得 0；`Int`：`0-5` 在 Lean 得 -5，FLOW 得 0；`Float`：`3.0/2` 在 Lean 得 1.5，FLOW 得 1.0 |
| C2 | High | 運算子優先序與結合性錯誤 | `a + b <<< 2`、`a \|\|\| b &&& c`、`2^3^2`（Lean = 512，FLOW = 64） |
| C3 | High | Kernel 回傳型別不是純量時（如 Cedar 的 `isAuthorized`），**改用一段罐頭 kernel 字串取代**；parser 內還有寫死的 fixture 名稱（`"PolicyIdsUnique"`、`"Cedar.Thm"`） | Cedar 的「端到端」測試與 Cedar 語意無關 |
| C4 | High | Kernel 以 weak 符號連結：找不到符號時直接跳過 kernel 並回報成功；Python E2E 的斷言多半只是回顯輸入或 wrapper 自己算出的值 | 把 `krc` 寫死成 `0` 仍全綠 |
| C5 | Medium | 各處型別表不一致（UInt8/16、Float32 被當成 8 bytes）；Python 端把負數默默 clamp 成 0 | `canApprove(-5, …)` 是因為 -5 被轉成 0 才通過 |
| C6 | Medium | Unicode / 巢狀 if 的 kernel 只驗證能編譯，從未實際執行；JS / CUDA / MATLAB / Julia adapter 從未編譯過 | — |
| C7 | Medium | `strstr(line, cand_fn)`：`step` 會誤配到 `stepReflex`；kernel 是「最後一個 def」，不是 README 所說「被 theorem 引用的那個」 | — |

---

## 5. 並發 / QSBR / 記憶體

| ID | 嚴重度 | 問題 |
| :--- | :--- | :--- |
| Q1 | Critical | [`flow_qsbr_call`](file:///Users/kuangtalin/Documents/FLOW/src/reload.c#L99-L101) 沒有任何 reader 保護 → publish + reclaim 與 `run()` 並行時會發生 UAF |
| Q2 | Critical | Reader 的 lease 超過 200ms 後，reclaim 會直接釋放它仍在使用的記憶體 |
| Q3 | Critical | Stage 8 的 reclaim 在註冊任何 reader 之前就呼叫 → 把 `generation_safe` 改成 `return 1` 仍全綠；它還拿兩個毫無關聯的 epoch 計數器互相比較 |
| Q4 | High | `flow_qsbr_synchronize` 的等待迴圈從未被執行到；只追蹤前 64 個 reader |
| Q5 | High | Stage 2 宣稱「state migrated」，但 `publish` 從未呼叫 `migrate`，斷言的 105 其實證明了「沒有遷移」 |
| Q6 | Critical | `FLOW_ATOMIC_STAGE_SWAP` 是普通的 64B struct 複製加上 fence，不是原子操作（data race / tearing） |
| Q7 | High | Wavefront 的「implicit QSBR」每一步都無條件 fold arena，仍被引用的指標會被重用 |
| Q8 | High | 「<100ns hot-swap」= 寫死的 `84`；fvec 的 swap latency 寫死為 `12.5`；`tlb_shootdowns_avoided` 其實只是呼叫次數計數 |
| Q9 | High | JIT：W^X 只比對指標值，不查實際頁面權限；code heap 滿了會繞回開頭覆寫活的 code；編譯出的 code 從未被執行；`wx_msg` 殘留上一次的訊息讓 `strstr` 恆真 |
| Q10 | Medium | `flow_reload_begin` 的 seq_cst store 接 acquire load，存在 store-buffering 重排風險；swarm seqlock 缺 fence；audit logger 是沒有同步保護的全域變數 |
| Q11 | Medium | 錯誤路徑（`INCOMPATIBLE`、`BUSY`、`NO_CURRENT`、init 失敗、double unregister）完全沒有測試 |

---

## 6. 安全閘門與檔案格式

| ID | 嚴重度 | 問題 |
| :--- | :--- | :--- |
| E1 | Critical | `.fjet` 的 legacy 分支不驗證 CRC；header（`hamiltonian_energy`、`vector_dim`）無完整性保護；沒有任何「壞檔案」測試 |
| E2 | High | NaN 能通過 attractor IDS、continuity 檢查與 embodied 的扭矩閘門（[embodied.c:27](file:///Users/kuangtalin/Documents/FLOW/plugins/embodied.c#L27)），這是**安全關鍵**問題 |
| E3 | High | 只要把 header 的 `hamiltonian_energy` 設成 ≤0，物理屏障就被關閉 |
| E4 | High | Embodied 的 ZMP 檢查失敗前，state 已經寫入（「prevented」不實） |
| E5 | High | IMU 收到 `inf` 或超大的陀螺儀值時，`while (roll > π) roll -= 2π` 會**無窮迴圈** |
| E6 | High | CAN（Linux 路徑）以 `len` ≤ 255 對 64B buffer 做 `memcpy` 會溢位；`len < 8`、ring 滿、非法 mode、NaN 等情況都沒有測試；feedback ID 與 command ID 會碰撞 |
| E7 | High | `flow_registry_load_dso` 會 dlopen 任意路徑，在 ABI 檢查**之前**就執行了 DSO 的 constructor，而且沒有任何測試 |
| E8 | Medium | `flowy ui` 把未經跳脫的路徑直接拼進 shell 指令與 Python 原始碼（注入風險） |

---

## 7. 領域模組（tautology / 只測 happy path）

| ID | 嚴重度 | 問題 |
| :--- | :--- | :--- |
| D1 | High | `flow_bitspace_search` 忽略 `iterations` 與 `seed`，實際上沒有搜尋 |
| D2 | High | Moreau 測試的輸入序列無法區分 hysteresis 與單純的 comparator（hysteresis 的記憶從未被驗證） |
| D3 | High | Polyhedral：`mask` 永遠不會是 0、`non_aliasing` 規則寫死為 `0`、`is_bounded` 恆真、`total *= span` 有 signed overflow |
| D4 | High | Cubical：De Morgan「定理」只是在測 C 的運算子；homotopy 測試拿 `p` 與 `p` 比較；把 intent projection 改成 `return ~0` 仍全綠；比對區分大小寫（`"QSBR"` 永遠不會命中） |
| D5 | High | Geometric：Section↔Jet round trip 遺失 48/64 個座標（測試只檢查 index 0，卻宣稱保留全部座標）；leapfrog 積分器從 q=p=0 起算，等於從未被執行 |
| D6 | High | `search.c:32` 無條件設定 `hard_gate_passed = 1` |
| D7 | High | fvec：`pure_genome` 沒有作用、invariant/malleable 角色疑似顛倒、文字 header 可以被注入欄位 |
| D8 | Medium | 位移量可能 ≥64（UB）分散在 polyhedral、search、bitspace、adaptive；`memory_limit_mb * 1024 * 1024` 有 int overflow |
| D9 | Medium | CAN decode 只驗了一半的欄位；ε 是 LSB 的 26 倍 |
| D10 | Medium | 依賴 repo 大小的斷言（`total_loc < 10000`）、計時斷言（加上 sanitizer 後會 flaky） |

---

## 8. 「突變存活」清單（每一條都應讓測試變紅，目前全綠）

1. `smt.c:35` → `return 1;`
2. `generation_safe` → `return 1;`
3. 刪除 `flow_qsbr_synchronize` 的等待迴圈
4. 刪除 `flow_jet.c:113` 的 CRC 比對
5. `backend.c` 的 `krc` → `0`（不管 kernel 結果）
6. `backend.c:274` 的 `flow_nat_sub` → 普通的 `-`
7. `flow_cubical_project_text_intent` → `return ~0ULL;`
8. `polyhedral.c:85` → `return full;`
9. Moreau → 單純的 comparator
10. 刪除 geometric 的 leapfrog 更新迴圈
11. 任一 `*_verify_smt` → `return 0;`（不寫入 `proof_out`）
12. `jit.c` 的頁面權限改成 RWX
13. `hardware_telemetry.c:31` 的預設功率改成 0.0001
14. `flowc.c:167` 的 `status` 寫死為 PROVEN
15. `driver_can.c` 對調 kp/kd 的打包順序
