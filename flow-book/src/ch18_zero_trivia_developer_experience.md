# 第十八章：零摩擦人機介面與工程水分擠乾 (Zero-Trivia CLI、宣告式自動坍縮與柯爾莫哥洛夫下限)

> 「程式碼行數即系統結構熵。使用者不需要記住繁瑣的命令列微調旗標；宣告你的幾何意圖，編譯器為你完成一切。」

---

## 18.1 零瑣事 (Zero Trivia) 命令列哲學

過去使用傳統編譯器或早期 FLOW 時，開發者需要手動輸入繁複參數：
```bash
# 舊有繁瑣命令（充滿瑣事壞味道）：
flowc examples/rank.flow -o generated/rank.c --search --iterations 500 --seed 42 --workload-bytes 1048576
```

在 FLOW 2.0 中，命令列介面全面重塑為零瑣事體驗：
```bash
# 現代零瑣事編譯：
flowc examples/rank.flow
# -> 自動推導輸出 rank.c，自動套用 rank.fvec，SMT 形式證明保證零缺陷！

# 現代零瑣事直接執行：
flowy run examples/rank.flow
# -> 一鍵編譯、驗證、建置 native 二進位並立即執行！

# 現代零瑣事建置二進位：
flowy build examples/rank.flow -o build/rank_app
```

---

## 18.2 擠乾軟體工程結構熵

FLOW 將長達數十年的工程妥協徹底擠乾：
1. **消滅 Allocator 階層**：幾何 Bump-Pointer + QSBR 世代折疊取代 Slab/Buddy。
2. **消滅防禦性條件瀑布**：Curry-Howard 前置證明使防禦檢查作為死碼消除。
3. **消滅序列化開銷**：Wire Format 與 Memory Struct 拓撲同構，$0\text{ ns}$ 解析。
4. **消滅配置檔案**：相空間能量極小化自發收斂，0 設定檔。
5. **消滅熱路徑日誌格式化**：64-bit 語義事件流形向量，1 個 CPU 週期發射。
6. **消滅文件代碼雙軌漂移**：Doc-as-Intent 文檔即規格，Markdown 直接原生編譯。

軟體系統由此回歸柯爾莫哥洛夫複雜度的理論下限，抵達純粹、強大且自洽的活體幾何宇宙。
