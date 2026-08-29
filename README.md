# indicator-china-weather

The weather data are from the QWeather (和风天气) API v7 (requires your own API key, see「和风天气凭据配置」below).

![](./doc/weather_zh_CN.png)

### Author's Home Page
 
[Eight Plus &rarr;](https://eightplus.github.io/)



### v4.0 test url（和风天气 API v7，需认证头 `X-QW-Api-Key: your-key`）

+ 实况天气：`https://devapi.qweather.com/v7/weather/now?location=101250101&lang=zh`

+ 天气预报（7 天，UI 显示 7 天）：`https://devapi.qweather.com/v7/weather/7d?location=101250101&lang=zh`

+ 实时空气质量：`https://devapi.qweather.com/v7/air/now?location=101250101&lang=zh`

+ 生活指数（1运动 2洗车 3穿衣 5紫外线 9感冒 10空气污染扩散）：`https://devapi.qweather.com/v7/indices/1d?location=101250101&type=1,2,3,5,9,10&lang=zh`

+ 城市信息查询：`https://geoapi.qweather.com/v2/city/lookup?location=changsha&lang=zh`

curl 示例（响应为 gzip，curl 加 `--compressed`）：

```bash
curl --compressed -H "X-QW-Api-Key: your-key" "https://devapi.qweather.com/v7/weather/now?location=101250101&lang=zh"
```

说明：location 使用 101 风格的 LocationID；图标代码体系（100-999）与下文图标表一致，`icon`/`iconDay`/`iconNight` 字段直接对应。


## Arch Linux 安装（本仓库打包）

本仓库提供 Arch Linux 打包文件（位于 `archlinux/` 目录），直接打包 `archlinux` 分支（本仓库）的本地代码：PKGBUILD 的 source 通过本地 git file:// 协议指向仓库根目录（PKGBUILD 所在 `archlinux/` 目录的上一级）并检出 `archlinux` 分支，构建全程不访问网络。

### 安装依赖

```bash
sudo pacman -S --needed base-devel git geoip qt6-base qt6-tools gsettings-qt6 kwindowsystem cmake extra-cmake-modules
```

+ 本分支 3.1.2 已移植到 Qt6 并迁移到 CMake 构建，并随包提供 Plasma 6 天气小部件（见下文「Plasma 小部件（Plasmoid）」章节）
+ 托盘应用的常驻托盘图标已下线，天气展示改由 Plasma 小部件承担；应用本体保留，可作为完整天气窗口启动
+ ukui-log4qt 为 UKUI 专属日志库，在 Arch 官方仓库与 AUR 均无包，属于可选依赖，本打包默认不启用（仅影响内部日志初始化，天气功能不受影响）

### 打包安装

```bash
git clone -b archlinux https://github.com/madoldman/indicator-china-weather.git
cd indicator-china-weather/archlinux
makepkg -si
```

检出 `archlinux` 分支后执行 `makepkg -si`，直接打包本仓库本地代码，全程不访问网络。

### 代码更新后重新打包

+ 代码改动提交/合入 `archlinux` 分支后，在 `archlinux/` 目录重跑 `makepkg -si` 即可，无需修改校验和

### 维护提示

+ 修改 PKGBUILD 后，在 `archlinux/` 目录执行 `makepkg --printsrcinfo > .SRCINFO` 重新生成元数据
+ 如已安装 namcap，可执行 `namcap archlinux/PKGBUILD` 做打包检查


## 和风天气凭据配置

应用直连和风天气（QWeather）API v7，需在运行环境中提供自己的凭据（不随源码/打包分发）：

+ `QWEATHER_API_KEY`（必须）：用于 `X-QW-Api-Key` 请求头认证，在和风天气控制台创建项目后获取
+ `QWEATHER_CREDENTIAL_ID`（可选）：凭据 ID，当前 API Key 认证方式不使用，仅作标识保存（供未来 JWT 认证或日志标识）

推荐写入 `~/.profile`：

```bash
export QWEATHER_API_KEY="你的API_KEY"
export QWEATHER_CREDENTIAL_ID="你的凭据ID"
```

生效方式说明：

+ 重新登录桌面会话后生效（SDDM/Plasma 登录时会读取 `~/.profile`，开机自启的应用继承会话环境）
+ 也可使用 `~/.config/environment.d/qweather.conf`（systemd 用户会话）方式，写入 `QWEATHER_API_KEY=你的API_KEY` 与 `QWEATHER_CREDENTIAL_ID=你的凭据ID`
+ 从终端手动启动时，确保该终端已重新登录或 source 过上述文件
+ 未设置 `QWEATHER_API_KEY` 时，应用启动日志会输出中文告警并跳过全部天气请求，界面保持无数据状态（不会崩溃）


## Plasma 小部件（Plasmoid）

本仓库自带一个标准 Plasma 6 桌面小部件 `org.madoldman.chinaweather`（plasmoid/package/），随 archlinux 打包一并安装：

+ `/usr/share/plasma/plasmoids/org.madoldman.chinaweather/`：小部件本体（QML 界面、配置页、天气图标与城市表）
+ `/usr/lib/qt6/qml/org/madoldman/chinaweather/`：C++ QML 扩展模块（`WeatherClient` 后端，直连和风天气 API v7）

### 添加到面板

1. 右键点击面板空白处，选择「添加部件」（或「进入编辑模式」->「添加部件」）
2. 搜索「天气」（或 `chinaweather`）
3. 将「天气」小部件拖到面板任意位置

小部件在面板上显示「天气图标 + 当前温度」，图标随当前天气实时变化（和风 100-999 天气代码，夜间自动切换夜间图标）。点击后面板会在小部件所在位置弹出完整天气面板（由 Plasma 原生锚定弹出，包含城市名、实况天气、7 天预报、空气质量 AQI 与 6 项生活指数），再次点击或点击面板外空白处收起。

### 右键菜单

小部件右键菜单在 Plasma 默认项之外新增两项：

+ 「打开应用」：启动本仓库的 Qt 应用（indicator-china-weather）并显示完整天气窗口；应用已在运行时则唤起其主窗口
+ 「设置」：打开小部件的配置对话框（等同 Plasma 默认的「配置 天气…」入口）

### 配置项

右键 -> 「设置」（或「配置 天气…」）可配置：

+ **城市（默认自动定位）**：默认不写死任何城市，按当前公网 IP 自动解析所在城市（主源 `geoip.ubuntu.com/lookup`，备源 `myip.ipip.net`，均无需凭据），解析结果仅缓存在本次会话内存中，不写入配置--换网/移动城市后重启小部件即自动更新；自动模式下城市名带「·自动」标识。也可输入城市名 / 拼音 / 拼音缩写搜索（如 `北京` / `changsha` / `bj`，基于本地 china-city-list.csv，不发网络请求）手动选择，选择后固定使用该配置；点击「恢复自动定位」清空配置回到自动模式
+ **刷新间隔**：自动刷新周期，单位分钟，默认 30（保存后立即按新间隔生效）

IP 定位失败或定位到的城市不在本地城市表中时，面板会显示明确的中文错误提示（不会静默回落到任何写死城市），此时可在设置中手动选择城市。

### 凭据要求

小部件的数据后端与托盘应用共用同一凭据环境变量（缺失时小部件面板会显示错误提示且不发起请求），配置方式见上文「和风天气凭据配置」章节：Plasma 会话从 `~/.config/environment.d/qweather.conf` 或 `~/.profile` 继承 `QWEATHER_API_KEY`，修改后重新登录生效。

### 托盘应用说明

Qt 应用（indicator-china-weather）的常驻系统托盘图标已下线：托盘形态被 Plasma 小部件取代。应用本体保留，可从应用启动器或小部件右键菜单「打开应用」启动，用于查看完整天气窗口（多城市收藏等原主窗口功能）。

### 开发调试

```bash
# 从源码目录临时加载（不安装）查看小部件
QML2_IMPORT_PATH=plasmoid/build-qml plasmoidviewer -a plasmoid/package

# 安装/更新到系统（需 root）
sudo kpackagetool6 -t Plasma/Applet -u plasmoid/package
```


### Internationalization

1. lupdate indicator-china-weather.pro
2. linguist translation/indicator-china-weather_zh_CN.ts
3. lrelease indicator-china-weather.pro


### Lintian

lintian -i -EvIL +pedantic --verbose ../indicator-china-weather_3.0.0_amd64.changes




### 和风天气图标
[官方地址](https://dev.heweather.com/docs/refer/condition)

#### 下载
[图片打包下载](https://cdn.heweather.com/cond-icon-heweather.zip)

#### 使用
* 图标文件名为天气代码，后缀为.png
* 图标文件名中有字母`n`的，为夜间天气图标，例如[100n.png](https://cdn.heweather.com/cond_icon/100n.png "晴天图标")

#### 天气代码对照表
| 代码 | 中文 | 英文 | 图标 |
|---|---|---|---|
| 100 | 晴 | Sunny/Clear |[100.png](https://cdn.heweather.com/cond_icon/100.png "晴天图标")|
| 101 | 多云 | Cloudy | [101.png](https://cdn.heweather.com/cond_icon/101.png "多云图标") |
| 102 | 少云 | Few Clouds | [102.png](https://cdn.heweather.com/cond_icon/102.png "少云图标") |
| 103 | 晴间多云 | Partly Cloudy | [103.png](https://cdn.heweather.com/cond_icon/103.png "晴间多云图标") |
| 104 | 阴 | Overcast | [104.png](https://cdn.heweather.com/cond_icon/104.png "阴图标") |
| 200 | 有风 | Windy | [200.png](https://cdn.heweather.com/cond_icon/200.png "有风图标") |
| 201 | 平静 | Calm | [201.png](https://cdn.heweather.com/cond_icon/201.png "平静图标") |
| 202 | 微风 | Light Breeze | [202.png](https://cdn.heweather.com/cond_icon/202.png "微风图标") |
| 203 | 和风 | Moderate/Gentle Breeze | [203.png](https://cdn.heweather.com/cond_icon/203.png "和风图标") |
| 204 | 清风 | Fresh Breeze | [204.png](https://cdn.heweather.com/cond_icon/204.png "清风图标") |
| 205 | 强风/劲风 | Strong Breeze | [205.png](https://cdn.heweather.com/cond_icon/205.png "强风图标") |
| 206 | 疾风 | High Wind, Near Gale | [206.png](https://cdn.heweather.com/cond_icon/206.png "疾风图标") |
| 207 | 大风 | Gale | [207.png](https://cdn.heweather.com/cond_icon/207.png "大风图标") |
| 208 | 烈风 | Strong Gale | [208.png](https://cdn.heweather.com/cond_icon/208.png "烈风图标") |
| 209 | 风暴 | Storm | [209.png](https://cdn.heweather.com/cond_icon/209.png "风暴图标") |
| 210 | 狂爆风 | Violent Storm | [210.png](https://cdn.heweather.com/cond_icon/210.png "狂爆风图标") |
| 211 | 飓风 | Hurricane | [211.png](https://cdn.heweather.com/cond_icon/211.png "飓风图标") |
| 212 | 龙卷风 | Tornado | [212.png](https://cdn.heweather.com/cond_icon/212.png "龙卷风图标") |
| 213 | 热带风暴 | Tropical Storm | [213.png](https://cdn.heweather.com/cond_icon/213.png "热带风暴图标") |
| 300 | 阵雨 | Shower Rain | [300.png](https://cdn.heweather.com/cond_icon/300.png "阵雨图标") |
| 301 | 强阵雨 | Heavy Shower Rain | [301.png](https://cdn.heweather.com/cond_icon/301.png "强阵雨图标") |
| 302 | 雷阵雨 | Thundershower | [302.png](https://cdn.heweather.com/cond_icon/302.png "雷阵雨图标") |
| 303 | 强雷阵雨 | Heavy Thunderstorm | [303.png](https://cdn.heweather.com/cond_icon/303.png "强雷阵雨图标") |
| 304 | 雷阵雨伴有冰雹 | Hail | [304.png](https://cdn.heweather.com/cond_icon/304.png "雷阵雨伴有冰雹图标") |
| 305 | 小雨 | Light Rain | [305.png](https://cdn.heweather.com/cond_icon/305.png "小雨图标") |
| 306 | 中雨 | Moderate Rain | [306.png](https://cdn.heweather.com/cond_icon/306.png "中雨图标") |
| 307 | 大雨 | Heavy Rain | [307.png](https://cdn.heweather.com/cond_icon/307.png "大雨图标") |
| 308 | 极端降雨 | Extreme Rain | [308.png](https://cdn.heweather.com/cond_icon/308.png "极端降雨图标") |
| 309 | 毛毛雨/细雨 | Drizzle Rain | [309.png](https://cdn.heweather.com/cond_icon/309.png "毛毛雨图标") |
| 310 | 暴雨 | Storm | [310.png](https://cdn.heweather.com/cond_icon/310.png "暴雨图标") |
| 311 | 大暴雨 | Heavy Storm | [311.png](https://cdn.heweather.com/cond_icon/311.png "大暴雨图标") |
| 312 | 特大暴雨 | Severe Storm | [312.png](https://cdn.heweather.com/cond_icon/312.png "特大暴雨图标") |
| 313 | 冻雨 | Freezing Rain | [313.png](https://cdn.heweather.com/cond_icon/313.png "冻雨图标") |
| 314 | 小到中雨 | Light to moderate rain | [314.png](https://cdn.heweather.com/cond_icon/314.png "小到中雨图标") |
| 315 | 中到大雨 | Moderate to heavy rain | [315.png](https://cdn.heweather.com/cond_icon/315.png "中到大雨图标") |
| 316 | 大到暴雨| Heavy rain to storm | [316.png](https://cdn.heweather.com/cond_icon/316.png "大到暴雨图标") |
| 317 | 暴雨到大暴雨 | Storm to heavy storm | [317.png](https://cdn.heweather.com/cond_icon/317.png "暴雨到大暴雨图标") |
| 318 | 大暴雨到特大暴雨 | Heavy to severe storm | [318.png](https://cdn.heweather.com/cond_icon/318.png "大暴雨到特大暴雨图标") |
| 399 | 雨 | Rain | [399.png](https://cdn.heweather.com/cond_icon/399.png "雨图标") |
| 400 | 小雪 | Light Snow | [400.png](https://cdn.heweather.com/cond_icon/400.png "小雪图标") |
| 401 | 中雪 | Moderate Snow | [401.png](https://cdn.heweather.com/cond_icon/401.png "中雪图标") |
| 402 | 大雪 | Heavy Snow | [402.png](https://cdn.heweather.com/cond_icon/402.png "大雪图标") |
| 403 | 暴雪 | Snowstorm | [403.png](https://cdn.heweather.com/cond_icon/403.png "暴雪图标") |
| 404 | 雨夹雪 | Sleet | [404.png](https://cdn.heweather.com/cond_icon/404.png "雨夹雪图标") |
| 405 | 雨雪天气 | Rain And Snow | [405.png](https://cdn.heweather.com/cond_icon/405.png "雨雪天气图标") |
| 406 | 阵雨夹雪 | Shower Snow | [406.png](https://cdn.heweather.com/cond_icon/406.png "阵雨夹雪图标") |
| 407 | 阵雪 | Snow Flurry | [407.png](https://cdn.heweather.com/cond_icon/407.png "阵雪图标") |
| 408 | 小到中雪 | Light to moderate snow | [408.png](https://cdn.heweather.com/cond_icon/408.png "小到中雪图标") |
| 409 | 中到大雪 | Moderate to heavy snow | [409.png](https://cdn.heweather.com/cond_icon/409.png "中到大雪图标") |
| 410 | 大到暴雪 | Heavy snow to snowstorm | [410.png](https://cdn.heweather.com/cond_icon/410.png "大到暴雪图标") |
| 499 | 雪 | Snows | [499.png](https://cdn.heweather.com/cond_icon/499.png "雪图标") |
| 500 | 薄雾 | Mist | [500.png](https://cdn.heweather.com/cond_icon/500.png "薄雾图标") |
| 501 | 雾 | Foggy | [501.png](https://cdn.heweather.com/cond_icon/501.png "雾图标") |
| 502 | 霾 | Haze | [502.png](https://cdn.heweather.com/cond_icon/502.png "霾图标") |
| 503 | 扬沙 | Sand | [503.png](https://cdn.heweather.com/cond_icon/503.png "扬沙图标") |
| 504 | 浮尘 | Dust | [504.png](https://cdn.heweather.com/cond_icon/504.png "浮尘图标") |
| 507 | 沙尘暴 | Duststorm | [507.png](https://cdn.heweather.com/cond_icon/507.png "沙尘暴图标") |
| 508 | 强沙尘暴 | Sandstorm | [508.png](https://cdn.heweather.com/cond_icon/508.png "强沙尘暴图标") |
| 509 | 浓雾 | Dense fog | [509.png](https://cdn.heweather.com/cond_icon/509.png "浓雾图标") |
| 510 | 强浓雾 | Strong fog | [510.png](https://cdn.heweather.com/cond_icon/510.png "强浓雾图标") |
| 511 | 中度霾 | Moderate haze | [511.png](https://cdn.heweather.com/cond_icon/511.png "中度霾图标") |
| 512 | 重度霾 | Heavy haze | [512.png](https://cdn.heweather.com/cond_icon/512.png "重度霾图标") |
| 513 | 严重霾 | Severe haze | [513.png](https://cdn.heweather.com/cond_icon/513.png "重度霾图标") |
| 514 | 大雾 | Heavy fog | [514.png](https://cdn.heweather.com/cond_icon/514.png "大雾图标") |
| 515 | 特强浓雾 | Extra heavy fog | [515.png](https://cdn.heweather.com/cond_icon/515.png "特强浓雾图标") |
| 900 | 热 | Hot | [900.png](https://cdn.heweather.com/cond_icon/900.png "热图标") |
| 901 | 冷 | Cold | [901.png](https://cdn.heweather.com/cond_icon/901.png "冷图标") |
| 999 | 未知 | Unknown | [999.png](https://cdn.heweather.com/cond_icon/999.png "未知图标") |
