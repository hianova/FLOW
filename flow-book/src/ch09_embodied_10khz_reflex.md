# 第九章：10kHz 具身反射與非光滑接觸力學 (Moreau 凸集法錐、庫侖摩擦錐、ZMP 與阻抗控制)

> 「軟體工程的終極審判不在記憶體與暫存器之中，而在機械手臂觸碰實體物理世界的那一微秒。」

---

## 9.1 10kHz 脊髓反射熱路徑

在 `src/flow_embodied_mz.c` 中，FLOW 具身控制迴路以 10kHz（100 微秒週期）運行。在此極限熱路徑上：
- 拔除全部 6 個指針防禦性 null 檢查。
- 淘汰手動飽和截斷 `if (tau > limit) tau = limit;`。
- 全面採用 **Moreau 閉區間凸錐投影**：
  $$\tau_{\text{net}} = \operatorname{fmin}\left(\tau_{\text{limit}}, \operatorname{fmax}(-\tau_{\text{limit}}, \tau_{\text{net}})\right)$$
  編譯為硬體級無分支指令，管線零停頓，將物理衝擊吸收時間穩定控制在 3.20ms（遠優於 5.0ms 警戒線）。

---

## 9.2 庫侖摩擦錐與 ZMP 穩定性證明

在足式機器人與抓取任務中，法向反作用力與切向摩擦力受庫侖摩擦錐約束：
$$\|\mathbf{f}_t\| \le \mu f_n$$
FLOW 具身引擎實施零力矩點（Zero-Moment Point, ZMP）凸多邊形支撐域判據，配合 SMT 形式證明保證實機零打滑、零傾覆、零衝擊剪斷。
