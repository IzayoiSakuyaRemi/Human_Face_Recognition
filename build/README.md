ESP32-P4 监控面板 - 构建记录
==============================

项目路径: D:\WORKS\Human_Face_Recognition_site
远程仓库: https://github.com/IzayoiSakuyaRemi/Human_Face_Recognition (branch: site)

目录结构
--------
app.py          - Flask 后端
data.db         - SQLite 数据库
requirements.txt - Python 依赖
templates/
  index.html    - 前端页面（最终版）
build/          - 构建过程中使用的脚本

操作步骤
--------

1. 克隆仓库
   git clone --branch site https://github.com/.../Human_Face_Recognition.git

2. 安装依赖
   pip install flask

3. 启动服务
   cd Human_Face_Recognition_site
   python app.py
   访问 http://localhost:8765

设计迭代
--------

v1 (write_html.py)     - 赛博朋克风格: 网格背景、霓虹发光、粒子效果
v2 (modify_html.py)    - 增强背景: 动态扫描线、环境光晕、更多粒子
v3 (write_html2.py)    - 微调赛博朋克配色
v4 (write_html3.py)    - 深色高级感 + 金色点缀 + 时间线事件流
v5 (write_html4.py)    - 浅色优雅风: 深蓝 + 金色、Hero 卡片布局
v6 (modify_html2.py)   - 拉大模块间距
v7 (write_html5.py + modify_snap.py) - 全屏滚动吸附布局
v8 (modify_rich.py)    - 丰富视觉层次: 渐变色背景、光晕装饰、点阵纹理

最终设计
--------
- 浅色背景，深蓝(#1e3a5f)主色 + 金色点缀
- 两屏全屏滚动(snap scroll):
  第一屏: 仪表盘 - 大标题 + 状态 + 三个数据指标
  第二屏: 事件流 - 时间线列表
- 渐变背景、微光晕、卡片顶部色条等细节丰富视觉
