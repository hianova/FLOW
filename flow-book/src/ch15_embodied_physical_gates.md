# 第十五章：具身智能與物理閘門 (Embodied) (相延遲、史密斯預測器與 Sim-to-Real 降級)

> 「當軟體進駐機器人軀體，任何一個演算法錯誤都可能導致幾十公斤的鋼鐵軀體失去平衡摔毀。FLOW 建立了微物理零倒地保證、雙速率頻率分離與史密斯死區補償，築起堅不可摧的物理安全閘門。」

---

## 15.1 軟體進入物理世界：具身智能的極限挑戰

當代具身智能（Embodied AI）與人形機器人控制系統面臨傳統軟體工程未曾遇見的三大物理嚴苛條件：

```text
具身物理世界的嚴苛約束:
1. 重力與動力學剛性約束: 零力矩點 (ZMP) 偏離支撐多邊形 1 公分，機器人即刻摔倒。
2. 匯流排相延遲 (Phase Lag): CAN / EtherCAT 匯流排存在 3ms 通訊死區，引發控制震盪發散。
3. 頻率矛盾: 脊髓反射需要 10kHz 極速低延遲，而步態大腦 JIT 重構需要 1Hz 的高階算力。
```

FLOW 打造了專門的具身物理防護外掛 **`flow.embodied`（`src/embodied.h` 與 `src/embodied.c`）**，將物理定律轉化為編譯期與運行期的硬遮罩閘門。

---

## 15.2 微物理安全閘門與 ZMP 零倒地驗證 (`FlowPhysicsEngine`)

在任何神經網路或 JIT 演算法生成的關節扭矩指令發送給電機前，必須通過 `flow_physics_is_zmp_stable()` 形式化幾何驗證：

```c
typedef struct {
    double joint_angles[FLOW_MAX_JOINTS];       /* 關節弧度 (rad) */
    double joint_velocities[FLOW_MAX_JOINTS];   /* 關節角速度 (rad/s) */
    double joint_torques[FLOW_MAX_JOINTS];      /* 關節扭矩 (N*m) */
    double max_torque_limit[FLOW_MAX_JOINTS];   /* 物理極限額定扭矩 */
    double center_of_mass[3];                   /* 質心位置 [x, y, z] */
    double zmp_position[2];                     /* 零力矩點 (ZMP) [x, y] */
    double support_polygon[8][2];               /* 腳底支撐多邊形凸包頂點 */
    double mass_kg;
} FlowRigidBodyState;
```

### ZMP 幾何包容判據

$$\mathbf{p}_{\text{ZMP}} = \left( x_{\text{CoM}} - \frac{\ddot{x}_{\text{CoM}}}{g + \ddot{z}_{\text{CoM}}} z_{\text{CoM}}, \quad y_{\text{CoM}} - \frac{\ddot{y}_{\text{CoM}}}{g + \ddot{z}_{\text{CoM}}} z_{\text{CoM}} \right) \in \mathcal{P}_{\text{support}}$$

若微物理模擬器預測該控制指令將導致 ZMP 逸出支撐凸包，**物理安全閘門在 1 個週期內直接強制裁切扭矩（Torque Clamping）**，確保絕對零倒地。

---

## 15.3 雙速率頻率分離架構 (Dual-Rate Control)

FLOW 提出了仿生學的雙速率分層控制模型：

```text
雙速率頻率分離控制時序:
┌────────────────────────────────────────────────────────────────────────┐
│ 1. 脊髓反射迴路 (Spinal Reflex Loop): 10,000 Hz (0.1ms 步長)           │
│    - 純 C 打造，零動態分配，極致 PID + 前饋扭矩補償                   │
│    - 負責毫秒級抗擾動與重力平衡                                       │
├────────────────────────────────────────────────────────────────────────┤
│ 2. 大腦皮質重構迴路 (Cortical JIT Loop): 10 Hz (100ms 步長)             │
│    - 負責高階步態規劃 (平地走 -> 爬樓梯 -> 障礙越野 -> 緊急支撐防護)  │
│    - 透過 QSBR 在背後平滑過渡 (Alpha Interpolation [0.0 .. 1.0])       │
└────────────────────────────────────────────────────────────────────────┘
```

大腦皮質的步態熱替換絕不阻塞脊髓的 10kHz 反射，達成了高階適應性與底層確定性的完美平衡。

---

## 15.4 史密斯預測器 (Smith Predictor) 與相延遲補償

在真實機器人硬體中，從感測器讀取到電機產生實際扭矩存在數毫秒的硬體傳輸死區時間（Dead Time $\tau_{\text{delay}}$）。若不加以補償，相位滯後（Phase Lag）將導致嚴重的自激震盪。

FLOW 依循奈奎斯特-夏農採樣定理，動態推導離散延遲步數：

$$d = \left\lceil \frac{\tau_{\text{delay}}}{\Delta t} \right\rceil$$

```text
史密斯預測器時延補償模型:
目標角度 ──► [ 脊髓控制器 C(s) ] ─┬─► [ 真實無延遲模型 G_0(s) ] ──(+)──► 預測無時延未來狀態
                                  │                                ▲
                                  │                                └──(-)──┐
                                  └─► [ 延遲模型 G_0(s) * e^{-sT} ] ───────┘
                                  │
                                  └──► (經 3ms CAN 匯流排) ──► [ 真實關節電機 ]
```

史密斯預測器（`FlowSmithPredictor`）在記憶體中維護了循環歷史緩衝區，在每個採樣週期主動預測 $d$ 步之後的關節角與速度，將控制相位的超前補償精確注入控制迴路中，徹底消除匯流排抖動。

---

## 15.5 突發衝擊與 Sim-to-Real 自主降級 (Emergency Brace)

當機器人遭遇未預期的重型物理撞擊（例如受外力劇烈推擠超過 5.0 kg 閾值）：
1. 能量管理器（`FlowThermalEnergyGovernor`）在 10 微秒內觸發震盪喚醒（Shock Wakeup）。
2. 系統瞬間否決當前複雜的步行 JIT 邏輯，熱切換至預編譯的 **`FLOW_GAIT_EMERGENCY_BRACE`（緊急支撐防護模式）**。
3. 四肢電機同時切換為高阻尼阻抗控制，保護昂貴的諧波減速機與感測器不受衝擊損壞。
