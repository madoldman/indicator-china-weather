/*
 * 天气代码（100-999，和风天气/和风 v7 通用体系）与包内图标文件的映射。
 *
 * 图标集取自本仓库 res/weather_icons（单色天气图标，与托盘应用同源）：
 *   icons/white/    深色主题/面板使用（75 张，含 7 张夜间变体）
 *   icons/darkgrey/ 浅色主题/面板使用（75 张，含 7 张夜间变体）
 * 展示用普通 Image 渲染完整 artwork（不做单色遮罩，避免 Kirigami.Icon
 * 把彩色/灰度图形渲染成剪影的问题）。
 */

// 注意：QML 的 JS 库导入只导出 var 与 function，不能使用 const/let
var KNOWN_ICONS = [
    "100", "100n", "101", "102", "103", "103n", "104", "104n",
    "200", "201", "202", "203", "204", "205", "206", "207", "208",
    "209", "210", "211", "212", "213",
    "300", "300n", "301", "301n", "302", "303", "304", "305", "306",
    "307", "308", "309", "310", "311", "312", "313", "314", "315",
    "316", "317", "318", "399",
    "400", "401", "402", "403", "404", "405", "406", "406n", "407",
    "407n", "408", "409", "410", "499",
    "500", "501", "502", "503", "504", "507", "508", "509", "510",
    "511", "512", "513", "514", "515",
    "900", "901", "999"
];

var NIGHT_ICONS = ["100n", "103n", "104n", "300n", "301n", "406n", "407n"];

// 6-18 点视为白天，与托盘应用 convertCodeToBackgroud 的时段划分保持一致
function isNightHour(hour) {
    return hour < 6 || hour > 18;
}

// 返回图标基础文件名（不含目录与扩展名）；夜间优先返回带 n 后缀的变体，
// 未知/缺失的天气代码统一回退到 999（未知）
function iconBase(code, isNight) {
    var base = String(code || "999");
    // 和风 v7 新增的夜间代码 150-155（晴/多云/少云/晴间多云/阴 的夜间形态，
    // 实测北京夜间返回 150）：映射回对应白天代码（-50）并按夜间处理，
    // 其中 100/103/104 恰好有专属夜间图标（100n/103n/104n）
    var num = parseInt(base, 10);
    if (num >= 150 && num <= 155) {
        base = String(num - 50);
        isNight = true;
    }
    if (isNight && NIGHT_ICONS.indexOf(base + "n") >= 0) {
        base = base + "n";
    }
    if (KNOWN_ICONS.indexOf(base) < 0) {
        base = "999";
    }
    return base;
}

// 返回图标相对本目录的路径（调用方用 Qt.resolvedUrl 解析）；
// 深色主题选 white 图标集，浅色主题选 darkgrey 图标集
function iconPath(code, isNight, isDarkTheme) {
    var dir = isDarkTheme ? "white" : "darkgrey";
    return "icons/" + dir + "/" + iconBase(code, isNight) + ".png";
}

// 依主题文字色亮度判断深浅色主题（YIQ 亮度公式；textColor 为 Kirigami.Theme.textColor）
function isDarkTheme(textColor) {
    return (textColor.r * 0.299 + textColor.g * 0.587 + textColor.b * 0.114) < 0.5;
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
