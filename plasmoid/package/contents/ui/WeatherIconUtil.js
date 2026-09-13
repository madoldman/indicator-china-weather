/*
 * 天气代码（100-999，和风天气/和风 v7 通用体系）与包内官方彩色图标文件
 * （icons/color/，取自 static.qweather.com 202106d 精灵图裁剪）的映射。
 *
 * 和风 v7 接口自带的昼夜形态已足够区分图标：now/24h 夜间返回 150-155 夜间码，
 * 7d 预报分 iconDay/iconNight 两列，因此 QML 侧只在实况图标上用 isNightHour
 * 做「跨午夜未刷新仍显示白天码」的兜底映射（见 officialCode 的 isNight 参数）。
 *
 * 展示用普通 Image 渲染完整 artwork（不做单色遮罩，避免 Kirigami.Icon
 * 把彩色/灰度图形渲染成剪影的问题）。
 */

// 6-18 点视为白天，与托盘应用 convertCodeToBackgroud 的时段划分保持一致
function isNightHour(hour) {
    return hour < 6 || hour > 18;
}

// 官方彩色图标集只有 44 个代码：100-104 晴云系、150-154 夜间系、300-318 部分
// 雨、399、400-410 部分雪、499、500-515 部分雾霾、900/901/999。包内 icons/color/
// 缺少对应 PNG 的代码在此映射到官方最接近的彩色码（每个保留项的键码都确实没有
// 安装文件，缺口随图标集补齐后应同步删除映射）；其余代码一律渲染自己的 PNG
var OFFICIAL_FALLBACK = {
    "200": "999", "201": "999", "202": "999", "203": "999", "204": "999",
    "205": "999", "206": "999", "207": "999", "208": "999", "209": "999",
    "210": "999", "211": "999", "212": "999", "213": "999",
    "505": "503", "506": "507"
};

// 旧夜间码（100/101/102/103/104 出现在夜间实况时）→ 官方夜间码 150-154
var NIGHT_TO_OFFICIAL = { "100": "150", "101": "150", "102": "152", "103": "153", "104": "154" };

// 把任意天气代码解析为官方彩色图标文件名（不含扩展名）：
// - v7 夜间码 150-155 直接用官方夜间码（155 超出官方集，钳到 154）
// - isNight 为真时把白天码 100-104 经 NIGHT_TO_OFFICIAL 映射为夜间码；该兜底
//   仅用于实况图标（now 接口夜间本就返回 150-155），预报图标一律传 false
function officialCode(code, isNight) {
    var c = String(code || "999");
    var num = parseInt(c, 10);
    if (num >= 150 && num <= 155) {
        return String(Math.min(num, 154)); // v7 夜间码直接用官方夜间码
    }
    if (isNight && NIGHT_TO_OFFICIAL[c]) {
        return NIGHT_TO_OFFICIAL[c];
    }
    var fb = OFFICIAL_FALLBACK[c];
    return fb ? fb : c;
}

// 返回彩色图标相对本目录的路径（调用方用 Qt.resolvedUrl 解析）
function iconPath(code, isNight) {
    return "icons/color/" + officialCode(code, isNight) + ".png";
}

// 和风空气质量类别对应的展示色（近似国标 AQI 分级配色）
var AQI_COLORS = {
    "优": "#27ae60",
    "良": "#f1c40f",
    "轻度污染": "#f39c12",
    "中度污染": "#e67e22",
    "重度污染": "#9b59b6",
    "严重污染": "#c0392b"
};

// 未知类别返回 null，调用方应回退到 Kirigami.Theme.textColor
function aqiColor(category) {
    return AQI_COLORS[category] || null;
}
