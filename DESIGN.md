# Design System

## Theme

以“像一张纸放在桌上”为核心。默认使用低饱和米纸主题，同时提供素白与夜墨主题。界面保持不透明以保证 ClearType 文本清晰。

## Color

### 米纸（默认）

- Background: `#FBF7EE`
- Border: `#EDE4D2`
- Text: `#33302A`
- Secondary: `#756D60`
- Placeholder: `#786F62`
- Accent: `#BE7B4A`
- Accent surface: `#F4E5D6`
- Field: `#F5EEE0`

### 素白

- Background: `#FFFFFF`
- Border: `#E9EAEE`
- Text: `#1B1C1F`
- Secondary: `#6B6F78`
- Accent: `#3B6EF5`
- Accent surface: `#EBF0FF`

### 夜墨

- Background: `#1E1F23`
- Border: `#2D2F35`
- Text: `#E9E7E3`
- Secondary: `#8A8D95`
- Accent: `#E3B072`
- Accent surface: `#322B22`

## Typography

使用 Windows 系统字体 `Segoe UI`，中文回退至 `Microsoft YaHei UI`。日期 15 px、正文 12 px、辅助文字 10–11 px；随窗口 DPI 等比缩放。

## Layout

- 默认窗口：300 × 400 px
- 可调整宽度：240–480 px
- 圆角：Windows 11 使用 DWM 系统圆角；Windows 10 保持直角
- 标题区：40 px
- 任务行：32 px
- 输入框：30 px
- 底栏：29 px
- 跨日横幅：约 90 px，在窗口内部推开内容，不弹模态窗口

## Components

- 日期标题与桌面/置顶状态
- 今日进度文本及 3 px 进度条
- 单行原生输入区，视觉上融入便签
- 圆形勾选框任务行，悬停时显示拖动和删除操作
- 跨日延续横幅：延续、不延续、30 分钟后
- 托盘菜单：显示、桌面模式、置顶、开机启动、主题、历史、重置、退出

## Motion

只为状态变化使用 120–180 ms 的淡入或位移；系统启用减少动画时立即切换。首版不使用装饰性动画。
