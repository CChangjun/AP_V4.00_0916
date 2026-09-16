from __future__ import annotations

import hashlib
import io
import math
import os
import re
import sys
import zipfile
import xml.etree.ElementTree as ET
from pathlib import Path


ROOT = Path(__file__).resolve().parent
LOCAL_PACKAGES = ROOT / ".docgen_pkgs"
if LOCAL_PACKAGES.exists():
    sys.path.insert(0, str(LOCAL_PACKAGES))

import xlsxwriter
from PIL import Image, ImageDraw, ImageFilter, ImageFont


OUT = ROOT / "AP_WOS_Peer_AP_Memory_Protocol_Rev2.00.xlsx"
BUILD = ROOT / "AP_WOS_Peer_AP_Memory_Protocol_Rev2.00.xlsx.build"
PREVIEW_1 = ROOT / "_preview_AP_memory_architecture.png"
PREVIEW_2 = ROOT / "_preview_transaction_lifecycle.png"

MALGUN = Path(r"C:\Windows\Fonts\malgun.ttf")
MALGUN_B = Path(r"C:\Windows\Fonts\malgunbd.ttf")
TIMES = Path(r"C:\Windows\Fonts\times.ttf")
TIMES_B = Path(r"C:\Windows\Fonts\timesbd.ttf")
TIMES_I = Path(r"C:\Windows\Fonts\timesi.ttf")

PALETTE = {
    "ink": "#1D2733",
    "navy": "#18364F",
    "navy2": "#294E6A",
    "steel": "#5F7486",
    "slate": "#758492",
    "blue": "#EAF0F4",
    "blue2": "#D8E3EA",
    "teal": "#367787",
    "teal_light": "#E2EEF0",
    "gold": "#B68A43",
    "gold_light": "#F3ECDD",
    "gray": "#F4F5F6",
    "gray2": "#E3E6E8",
    "line": "#AEB8BF",
    "white": "#FFFFFF",
    "warn": "#8B3E38",
    "warn_light": "#F4E7E5",
    "shadow": "#1A2732",
}

HANGUL_RE = re.compile(r"[\u1100-\u11ff\u3130-\u318f\uac00-\ud7af]")
LATIN_RE = re.compile(r"[A-Za-z0-9]")


def hex_rgb(value: str):
    value = value.lstrip("#")
    return tuple(int(value[i:i + 2], 16) for i in (0, 2, 4))


def rgba(value: str, alpha=255):
    return (*hex_rgb(value), alpha)


def pil_font(size, *, bold=False, latin=False, italic=False):
    if latin:
        path = TIMES_I if italic else (TIMES_B if bold else TIMES)
    else:
        path = MALGUN_B if bold else MALGUN
    return ImageFont.truetype(str(path), size)


def gradient_box(img, box, top, bottom, *, radius=22, outline=None, width=3, shadow=True):
    x1, y1, x2, y2 = map(int, box)
    if shadow:
        layer = Image.new("RGBA", img.size, (0, 0, 0, 0))
        ld = ImageDraw.Draw(layer)
        ld.rounded_rectangle((x1 + 10, y1 + 12, x2 + 10, y2 + 12), radius=radius, fill=rgba(PALETTE["shadow"], 75))
        layer = layer.filter(ImageFilter.GaussianBlur(12))
        img.alpha_composite(layer)

    h = max(1, y2 - y1)
    c1, c2 = hex_rgb(top), hex_rgb(bottom)
    grad = Image.new("RGBA", (x2 - x1, h), (255, 255, 255, 255))
    gd = ImageDraw.Draw(grad)
    for y in range(h):
        t = y / max(1, h - 1)
        color = tuple(round(c1[i] * (1 - t) + c2[i] * t) for i in range(3))
        gd.line((0, y, x2 - x1, y), fill=(*color, 255))
    mask = Image.new("L", grad.size, 0)
    ImageDraw.Draw(mask).rounded_rectangle((0, 0, grad.width - 1, grad.height - 1), radius=radius, fill=255)
    img.paste(grad, (x1, y1), mask)
    d = ImageDraw.Draw(img)
    if outline:
        d.rounded_rectangle((x1, y1, x2, y2), radius=radius, outline=outline, width=width)


def solid_box(img, box, fill, *, outline=None, width=3, radius=20, shadow=True):
    gradient_box(img, box, fill, fill, radius=radius, outline=outline, width=width, shadow=shadow)


def text_center(img, box, text, font, fill, *, spacing=7):
    d = ImageDraw.Draw(img)
    x1, y1, x2, y2 = box
    bbox = d.multiline_textbbox((0, 0), text, font=font, align="center", spacing=spacing)
    w, h = bbox[2] - bbox[0], bbox[3] - bbox[1]
    d.multiline_text(((x1 + x2 - w) / 2, (y1 + y2 - h) / 2 - bbox[1]), text, font=font, fill=fill, align="center", spacing=spacing)


def text_left(img, xy, text, font, fill, *, spacing=7):
    ImageDraw.Draw(img).multiline_text(xy, text, font=font, fill=fill, spacing=spacing)


def wrap_text(img, text, font, max_width):
    d = ImageDraw.Draw(img)
    lines = []
    for paragraph in str(text).split("\n"):
        if not paragraph:
            lines.append("")
            continue
        current = ""
        for char in paragraph:
            trial = current + char
            if current and d.textlength(trial, font=font) > max_width:
                lines.append(current)
                current = char
            else:
                current = trial
        if current:
            lines.append(current)
    return "\n".join(lines)


def node(img, box, eng, kor, body, trace, *, accent="navy2", fill1="white", fill2="blue", warn=False):
    x1, y1, x2, y2 = box
    outline = PALETTE["warn"] if warn else PALETTE[accent]
    gradient_box(img, box, PALETTE[fill1], PALETTE[fill2], radius=20, outline=outline, width=3, shadow=True)
    d = ImageDraw.Draw(img)
    d.rounded_rectangle((x1, y1, x2, y1 + 50), radius=18, fill=outline)
    d.rectangle((x1, y1 + 25, x2, y1 + 50), fill=outline)
    text_center(img, (x1 + 8, y1 + 3, x2 - 8, y1 + 48), eng, pil_font(25, bold=True, latin=True), PALETTE["white"])
    text_center(img, (x1 + 10, y1 + 57, x2 - 10, y1 + 102), kor, pil_font(25, bold=True), PALETTE["ink"])
    body_font = pil_font(20)
    wrapped = wrap_text(img, body, body_font, (x2 - x1) - 28)
    text_center(img, (x1 + 14, y1 + 105, x2 - 14, y2 - 35), wrapped, body_font, PALETTE["ink"], spacing=5)
    text_center(img, (x1 + 8, y2 - 32, x2 - 8, y2 - 6), trace, pil_font(17, latin=True, italic=True), PALETTE["slate"], spacing=2)


def number_badge(img, x, y, number, color="gold"):
    d = ImageDraw.Draw(img)
    d.ellipse((x - 22, y - 22, x + 22, y + 22), fill=PALETTE[color], outline=PALETTE["white"], width=3)
    text_center(img, (x - 20, y - 20, x + 20, y + 20), str(number), pil_font(23, bold=True, latin=True), PALETTE["white"])


def flow_arrow(img, points, *, color="navy2", width=8, label=None, label_pos=None, dash=False):
    d = ImageDraw.Draw(img)
    pts = [(int(x), int(y)) for x, y in points]
    if dash:
        for p1, p2 in zip(pts[:-1], pts[1:]):
            x1, y1 = p1
            x2, y2 = p2
            length = math.hypot(x2 - x1, y2 - y1)
            if length == 0:
                continue
            ux, uy = (x2 - x1) / length, (y2 - y1) / length
            pos = 0
            while pos < length:
                end = min(pos + 18, length)
                d.line((x1 + ux * pos, y1 + uy * pos, x1 + ux * end, y1 + uy * end), fill=PALETTE[color], width=width)
                pos += 30
    else:
        d.line(pts, fill=PALETTE[color], width=width, joint="curve")
    x1, y1 = pts[-2]
    x2, y2 = pts[-1]
    angle = math.atan2(y2 - y1, x2 - x1)
    size = 22
    head = [
        (x2, y2),
        (x2 - size * math.cos(angle - math.pi / 6), y2 - size * math.sin(angle - math.pi / 6)),
        (x2 - size * math.cos(angle + math.pi / 6), y2 - size * math.sin(angle + math.pi / 6)),
    ]
    d.polygon(head, fill=PALETTE[color])
    if label and label_pos:
        lx, ly = label_pos
        f = pil_font(19, bold=True)
        b = d.textbbox((0, 0), label, font=f)
        w, h = b[2] - b[0] + 24, b[3] - b[1] + 14
        d.rounded_rectangle((lx - w / 2, ly - h / 2, lx + w / 2, ly + h / 2), radius=10, fill=PALETTE["white"], outline=PALETTE["line"], width=2)
        text_center(img, (lx - w / 2, ly - h / 2, lx + w / 2, ly + h / 2), label, f, PALETTE["ink"])


def matrix_icon(img, box, rows, cols, *, highlight=None, color="steel"):
    x1, y1, x2, y2 = box
    d = ImageDraw.Draw(img)
    cw = (x2 - x1) / cols
    ch = (y2 - y1) / rows
    for r in range(rows):
        for c in range(cols):
            fill = PALETTE["gold_light"] if highlight == (r, c) else PALETTE["white"]
            d.rectangle((x1 + c * cw, y1 + r * ch, x1 + (c + 1) * cw, y1 + (r + 1) * ch), fill=fill, outline=PALETTE[color], width=2)


def figure_heading(img, number, english, korean):
    d = ImageDraw.Draw(img)
    d.line((70, 94, img.width - 70, 94), fill=PALETTE["navy"], width=5)
    text_left(img, (70, 28), f"FIGURE {number}.  {english}", pil_font(37, bold=True, latin=True), PALETTE["navy"])
    text_left(img, (70, 108), korean, pil_font(28, bold=True), PALETTE["ink"])


def make_architecture_figure():
    img = Image.new("RGBA", (3600, 1740), rgba(PALETTE["white"]))
    figure_heading(
        img,
        "1",
        "AP-CENTRIC DATA FLOW AND MEMORY OWNERSHIP",
        "WOS 요청이 AP 내부 저장 블록을 통과해 Peer로 전송되고, 응답이 캐시에 적재되어 PDO로 반환되는 전체 경로",
    )
    d = ImageDraw.Draw(img)

    # External systems.
    gradient_box(img, (55, 500, 410, 980), PALETTE["navy2"], PALETTE["navy"], radius=28, outline=PALETTE["navy"], width=4, shadow=True)
    text_center(img, (80, 540, 385, 655), "WOS", pil_font(62, bold=True, latin=True), PALETTE["white"])
    text_center(img, (75, 650, 390, 715), "EtherCAT Master", pil_font(31, bold=True, latin=True), PALETTE["white"])
    text_center(img, (80, 735, 385, 835), "Output PDO : 요청\nInput PDO  : 응답", pil_font(25, bold=True), PALETTE["white"])
    text_center(img, (80, 880, 385, 945), "W00–W24 / 방향별 25 Word", pil_font(21), PALETTE["blue2"])

    gradient_box(img, (3180, 500, 3545, 980), PALETTE["teal"], PALETTE["navy2"], radius=28, outline=PALETTE["navy"], width=4, shadow=True)
    text_center(img, (3205, 540, 3520, 655), "PEER", pil_font(58, bold=True, latin=True), PALETTE["white"])
    text_center(img, (3205, 650, 3520, 720), "ESP-NOW Devices", pil_font(28, bold=True, latin=True), PALETTE["white"])
    text_center(img, (3210, 740, 3515, 845), "CH01–CH16\nI/O 또는 Serial 장치", pil_font(26, bold=True), PALETTE["white"])
    text_center(img, (3210, 885, 3515, 945), "응답 Payload 생성 주체", pil_font(21), PALETTE["teal_light"])

    # AP system boundary.
    solid_box(img, (520, 195, 3080, 1515), PALETTE["gray"], outline=PALETTE["navy"], width=6, radius=34, shadow=True)
    d.rounded_rectangle((520, 195, 3080, 285), radius=32, fill=PALETTE["navy"])
    d.rectangle((520, 245, 3080, 285), fill=PALETTE["navy"])
    text_left(img, (565, 216), "MCU(AP)", pil_font(45, bold=True, latin=True), PALETTE["white"])
    text_left(img, (790, 226), "— EtherCAT와 ESP-NOW 사이의 프로토콜 변환 및 데이터 소유권 경계", pil_font(26, bold=True), PALETTE["white"])
    text_left(img, (2625, 226), "SOURCE-BASED MODEL", pil_font(23, bold=True, latin=True), PALETTE["blue2"])

    # Request lane label.
    d.rounded_rectangle((565, 310, 775, 352), radius=12, fill=PALETTE["gold"])
    text_center(img, (565, 310, 775, 352), "REQUEST PATH", pil_font(23, bold=True, latin=True), PALETTE["white"])
    d.line((790, 331, 2990, 331), fill=PALETTE["gold"], width=4)

    nodes_top = [
        ((590, 380, 930, 620), "S1 · PDO RX IMAGE", "WOS 수신 프로세스 이미지", "W00–W24 / 25 Word\n요청 데이터의 최초 AP 소유 지점", "BufferOut.Cust"),
        ((1030, 380, 1370, 620), "WORD DECODER", "채널·Page·R/W 해석", "W00 Pairing\nW01–W16 I/O\nW17–W24 Serial", "Ethercat_Handle()"),
        ((1470, 380, 1810, 620), "S7 · REQUEST STAGING", "채널별 요청 임시 저장소", "I/O : 1 Word\nSerial : 9 Word\n전송 Payload의 현재 원본", "set_io_data / set_serial_Buf"),
        ((1910, 380, 2190, 620), "S8 · REQUEST FIFO", "송신 대기 순서", "Payload 사본이 아닌\n채널 번호만 저장\n49 entry 사용 가능", "wifi_send.queue"),
        ((2290, 380, 2630, 620), "S9 · RF FRAME BUFFER", "채널별 무선 프레임", "Header 6 byte\n+ Payload\n+ CRC16 2 byte\n최대 50 byte", "txBuf[channel]"),
        ((2730, 380, 3020, 620), "RF TRANSMITTER", "ESP-NOW 송신", "Peer MAC으로 전송\n응답 대기 상태 진입", "esp_now_send()"),
    ]
    for i, (box_, eng, kor, body, trace) in enumerate(nodes_top, start=1):
        node(img, box_, eng, kor, body, trace, accent="navy2", fill1="white", fill2="blue")
        number_badge(img, box_[0] + 5, box_[1] + 5, i)
        if i < len(nodes_top):
            flow_arrow(img, [(box_[2] + 8, 500), (nodes_top[i][0][0] - 8, 500)], color="navy2", width=8)

    # Request boundary arrows.
    flow_arrow(img, [(410, 610), (500, 610), (500, 500), (590, 500)], color="navy2", width=10, label="Output PDO", label_pos=(475, 565))
    flow_arrow(img, [(3020, 500), (3160, 500)], color="navy2", width=10, label="Request Frame", label_pos=(3090, 450))

    # Persistent state / ownership plane.
    d.rounded_rectangle((565, 660, 775, 702), radius=12, fill=PALETTE["steel"])
    text_center(img, (565, 660, 775, 702), "STATE & MEMORY", pil_font(22, bold=True, latin=True), PALETTE["white"])
    d.line((790, 681, 2990, 681), fill=PALETTE["steel"], width=4)

    node(img, (720, 735, 1120, 930), "S3 · PAIRING REGISTER", "Pairing 요청 저장변수", "16 bit bitmap\nbit n ↔ CH(n+1)\nW00의 AP 내부 보관값", "tx_pairStatus", accent="gold", fill1="white", fill2="gold_light")
    node(img, (1260, 735, 1680, 930), "S4 · IDENTITY TABLE", "채널 식별·기능표", "16 channel\nMAC / Type / ID / paired\nI/O Page / Serial 지원", "peer[16]", accent="steel", fill1="white", fill2="gray2")
    node(img, (1820, 735, 2220, 930), "S10 · TRANSACTION STATE", "채널별 거래 상태", "TX/RX busy\nrequest / response\ntimeout / retry", "tx_busy / rx_busy / peer_req", accent="steel", fill1="white", fill2="gray2")

    # State relations, deliberately dashed because they control rather than carry payload.
    flow_arrow(img, [(1200, 620), (1200, 690), (920, 690), (920, 735)], color="gold", width=5, dash=True)
    flow_arrow(img, [(1640, 620), (1640, 705), (1470, 705), (1470, 735)], color="steel", width=5, dash=True)
    flow_arrow(img, [(2050, 620), (2050, 735)], color="steel", width=5, dash=True)

    # Response lane and caches.
    d.rounded_rectangle((565, 980, 805, 1022), radius=12, fill=PALETTE["teal"])
    text_center(img, (565, 980, 805, 1022), "RESPONSE PATH", pil_font(23, bold=True, latin=True), PALETTE["white"])
    d.line((820, 1001, 2990, 1001), fill=PALETTE["teal"], width=4)

    node(img, (2590, 1060, 3020, 1350), "RX VALIDATION", "응답 프레임 검증", "길이 → Channel → Group\n→ RX busy → Type/ID\n→ CRC → Command/Length", "recv_cb()", accent="teal", fill1="white", fill2="teal_light")
    node(img, (2100, 1060, 2480, 1350), "S5 · I/O CACHE", "채널별 I/O 응답 캐시", "", "lPeerData[16][7]", accent="teal", fill1="white", fill2="teal_light")
    text_center(img, (2120, 1160, 2460, 1235), "16 channel × 7 Word\n비페이지=index 0 / Page형=index 1…6", pil_font(19), PALETTE["ink"], spacing=4)
    matrix_icon(img, (2150, 1240, 2430, 1325), 3, 7, highlight=(1, 2), color="teal")
    node(img, (1620, 1060, 1990, 1350), "S6 · SERIAL CACHE", "Serial 페이지 캐시", "", "lSPeerData[5][81]", accent="teal", fill1="white", fill2="teal_light")
    text_center(img, (1640, 1160, 1970, 1235), "활성 4 channel / Page 1…10\nPage당 8 Word (할당 5 × 81)", pil_font(19), PALETTE["ink"], spacing=4)
    matrix_icon(img, (1670, 1240, 1940, 1325), 4, 8, highlight=(1, 2), color="teal")
    node(img, (1130, 1060, 1510, 1350), "CACHE SELECTOR", "요청 채널·Page 선택", "I/O : 요청 Page index\nSerial : (Page−1)×8\n선택값을 송신 PDO에 복사", "rx_ptr / serial_ptr", accent="navy2", fill1="white", fill2="blue")
    node(img, (590, 1060, 1010, 1350), "S2 · PDO TX IMAGE", "WOS 송신 프로세스 이미지", "W00 Pairing 상태\nW01–W16 I/O 응답\nW17–W24 Serial 응답", "BufferIn.Cust", accent="navy2", fill1="white", fill2="blue")

    number_badge(img, 3015, 1060, 7, "teal")
    number_badge(img, 2475, 1060, 8, "teal")
    number_badge(img, 1985, 1060, 8, "teal")
    number_badge(img, 1505, 1060, 9, "teal")
    number_badge(img, 1005, 1060, 10, "teal")

    flow_arrow(img, [(3160, 875), (3060, 875), (3060, 1200), (3020, 1200)], color="teal", width=10, label="Response Frame", label_pos=(3110, 930))
    flow_arrow(img, [(2590, 1185), (2480, 1185)], color="teal", width=9)
    flow_arrow(img, [(2590, 1255), (2510, 1255), (2510, 1410), (1805, 1410), (1805, 1350)], color="teal", width=7, label="Serial", label_pos=(2180, 1410))
    flow_arrow(img, [(2100, 1185), (2040, 1185), (2040, 1028), (1545, 1028), (1545, 1185), (1510, 1185)], color="teal", width=8, label="I/O cache index", label_pos=(1795, 1028))
    flow_arrow(img, [(1620, 1275), (1510, 1275)], color="teal", width=9, label="Serial offset", label_pos=(1565, 1335))
    flow_arrow(img, [(1130, 1200), (1010, 1200)], color="teal", width=9)
    flow_arrow(img, [(590, 1200), (500, 1200), (500, 875), (410, 875)], color="teal", width=10, label="Input PDO", label_pos=(475, 1125))

    # Example callout.
    solid_box(img, (2350, 720, 2990, 920), PALETTE["white"], outline=PALETTE["gold"], width=3, radius=18, shadow=False)
    text_left(img, (2380, 742), "TRACE EXAMPLE · CH05 PAGE 2 READ", pil_font(24, bold=True, latin=True), PALETTE["gold"])
    text_left(img, (2380, 790), "W05=0x2000 → 임시 저장소 → FIFO[CH05] → AP_IO_GET", pil_font(21, bold=True), PALETTE["ink"])
    text_left(img, (2380, 832), "Peer 응답 → I/O 캐시[4][1…] → 캐시[4][2] → W05", pil_font(21, bold=True), PALETTE["ink"])
    text_left(img, (2380, 875), "※ 응답 Word는 Page tag로 재정렬되지 않고 수신 순서대로 저장", pil_font(19, bold=True), PALETTE["warn"])

    # Figure legend and caption.
    d.line((70, 1570, 3530, 1570), fill=PALETTE["line"], width=3)
    text_left(img, (75, 1590), "실선 화살표", pil_font(20, bold=True), PALETTE["navy2"])
    flow_arrow(img, [(215, 1605), (340, 1605)], color="navy2", width=6)
    text_left(img, (360, 1590), "데이터 또는 프레임의 이동", pil_font(20), PALETTE["ink"])
    text_left(img, (820, 1590), "점선 화살표", pil_font(20, bold=True), PALETTE["steel"])
    flow_arrow(img, [(965, 1605), (1090, 1605)], color="steel", width=5, dash=True)
    text_left(img, (1110, 1590), "상태 판단·검증·스케줄 제어", pil_font(20), PALETTE["ink"])
    text_left(img, (1900, 1590), "금색 번호", pil_font(20, bold=True), PALETTE["gold"])
    text_left(img, (2020, 1590), "요청 순서", pil_font(20), PALETTE["ink"])
    text_left(img, (2320, 1590), "청록 번호", pil_font(20, bold=True), PALETTE["teal"])
    text_left(img, (2460, 1590), "응답 순서", pil_font(20), PALETTE["ink"])
    text_left(img, (75, 1650), "Figure 1. AP는 단순 중계기가 아니라 요청의 임시 소유자, 채널 스케줄러, 응답 캐시의 소유자다. FIFO는 Payload가 아닌 채널 번호만 보관한다.", pil_font(23, italic=False), PALETTE["ink"])
    return img


def bit_strip(img, x, y, value, segment_colors, *, cell=41, label=""):
    d = ImageDraw.Draw(img)
    if label:
        text_left(img, (x, y - 55), label, pil_font(24, bold=True, latin=True), PALETTE["navy"])
    for i, bit in enumerate(range(15, -1, -1)):
        bx1 = x + i * cell
        fill = segment_colors.get(bit, PALETTE["white"])
        d.rectangle((bx1, y, bx1 + cell, y + 54), fill=fill, outline=PALETTE["steel"], width=2)
        text_center(img, (bx1, y - 24, bx1 + cell, y - 2), str(bit), pil_font(15, latin=True), PALETTE["slate"])
        text_center(img, (bx1, y, bx1 + cell, y + 54), str((value >> bit) & 1), pil_font(21, bold=True, latin=True), PALETTE["ink"])
    text_left(img, (x + cell * 16 + 18, y + 11), f"0x{value:04X}", pil_font(25, bold=True, latin=True), PALETTE["navy"])


def mini_step(img, box, heading, body, *, fill="white", accent="navy2", trace=""):
    x1, y1, x2, y2 = box
    gradient_box(img, box, PALETTE[fill], PALETTE["blue"] if fill == "white" else PALETTE[fill], radius=17, outline=PALETTE[accent], width=3, shadow=True)
    text_center(img, (x1 + 8, y1 + 10, x2 - 8, y1 + 52), heading, pil_font(23, bold=True), PALETTE[accent])
    text_center(img, (x1 + 12, y1 + 55, x2 - 12, y2 - 28), wrap_text(img, body, pil_font(19), x2 - x1 - 24), pil_font(19), PALETTE["ink"], spacing=4)
    if trace:
        text_center(img, (x1 + 6, y2 - 27, x2 - 6, y2 - 4), trace, pil_font(16, latin=True, italic=True), PALETTE["slate"])


def transaction_lane(img, y, code, title_eng, title_ko, steps, *, height=420, accent="navy2", note_text=None, bit_data=None):
    x1, x2 = 65, 3535
    solid_box(img, (x1, y, x2, y + height), PALETTE["white"], outline=PALETTE[accent], width=3, radius=24, shadow=True)
    d = ImageDraw.Draw(img)
    d.rounded_rectangle((x1, y, x1 + 280, y + 72), radius=22, fill=PALETTE[accent])
    d.rectangle((x1 + 130, y, x1 + 280, y + 72), fill=PALETTE[accent])
    text_center(img, (x1, y, x1 + 280, y + 72), code, pil_font(31, bold=True, latin=True), PALETTE["white"])
    text_left(img, (x1 + 315, y + 15), title_eng, pil_font(30, bold=True, latin=True), PALETTE["navy"])
    text_left(img, (x1 + 1250, y + 19), title_ko, pil_font(25, bold=True), PALETTE["ink"])
    count = len(steps)
    left, right, gap = 100, 3500, 38
    box_w = (right - left - gap * (count - 1)) / count
    top = y + 118
    bottom = y + 285
    boxes = []
    for i, (head, body, trace, fill) in enumerate(steps):
        bx1 = left + i * (box_w + gap)
        bx2 = bx1 + box_w
        boxes.append((bx1, top, bx2, bottom))
        mini_step(img, boxes[-1], head, body, fill=fill, accent=accent, trace=trace)
        if i < count - 1:
            flow_arrow(img, [(bx2 + 7, (top + bottom) / 2), (left + (i + 1) * (box_w + gap) - 7, (top + bottom) / 2)], color=accent, width=6)
    if bit_data:
        value, label, segment_colors = bit_data
        bit_strip(img, 120, y + 345, value, segment_colors, cell=37, label=label)
    if note_text:
        text_left(img, (1110, y + 327), wrap_text(img, note_text, pil_font(19, bold=True), 2330), pil_font(19, bold=True), PALETTE["warn"])


def make_transaction_figure():
    img = Image.new("RGBA", (3600, 2330), rgba(PALETTE["white"]))
    figure_heading(
        img,
        "2",
        "WORD-TO-MEMORY TRANSACTION LIFECYCLE",
        "WOS Word가 AP 내부 저장 블록에 기록되고 Peer 응답이 다시 PDO Word가 되기까지의 대표 거래별 추적",
    )

    transaction_lane(
        img, 180, "A", "PAIRING TRANSACTION", "CH05 · CDA(Type 0x02, ID 0x33) 등록",
        [
            ("WOS 작성", "W00 bit4=1\nW05=0x0233", "Output PDO", "white"),
            ("수신 이미지", "W00/W05를\n같은 검사 시점에 읽음", "S1 · BufferOut", "white"),
            ("요청 적재", "16 bit Pairing bitmap\n+ Type/ID Word", "S3 + S4 후보", "gold_light"),
            ("RF 전송", "CH=4, CMD=0x01\nType=0x02, ID=0x33", "AP_PAIRING_REQ", "white"),
            ("응답 적재", "MAC/Type/ID/paired\nI/O Page/Serial 기능", "S4 · peer[4]", "teal_light"),
            ("WOS 확인", "후속 유효 응답 후\nW00 bit4=1", "S2 · Input PDO", "white"),
        ],
        accent="gold",
        bit_data=(0x0010, "W00 PAIRING BITMAP", {4: PALETTE["gold_light"]}),
        note_text="0x81 Pairing 응답 직후가 아니라, 이후 정상 I/O 또는 Serial 응답을 확인한 시점에 W00 상태 bit가 설정된다.",
    )

    transaction_lane(
        img, 700, "B", "PAGED I/O READ", "CH05의 Page 2를 읽는 경우",
        [
            ("WOS 작성", "W05=0x2000\nR/W=0, Page=2", "Output PDO W05", "white"),
            ("요청 적재", "원본 16 bit를\n채널별 I/O 저장소에 보관", "S7 · set_io_data", "white"),
            ("송신 예약", "FIFO에는 0x2000이 아니라\nCH05 번호만 기록", "S8 · channel index", "gold_light"),
            ("RF 요청", "AP_IO_GET\nPayload 1 Word", "CMD=0x03", "white"),
            ("응답 캐시", "수신 Word를 순서대로\ncache[4][1…]에 복사", "S5 · lPeerData", "teal_light"),
            ("PDO 반환", "cache[4][2]\n→ AP→WOS W05", "S2 · BufferIn", "white"),
        ],
        accent="teal",
        bit_data=(0x2000, "W05 PAGE 2 READ", {15: PALETTE["warn_light"], 14: PALETTE["teal_light"], 13: PALETTE["teal_light"], 12: PALETTE["teal_light"]}),
        note_text="응답 내부의 Page tag를 찾아 재정렬하지 않는다. [Page 1][Page 4]만 오면 AP 캐시에는 [1][2] 순서로 붙는다.",
    )

    transaction_lane(
        img, 1220, "C", "PAGED I/O WRITE", "CH05의 Page 3에 Data 0x35A를 쓰는 경우",
        [
            ("WOS 작성", "W05=0xB35A\n1 | 011 | 0x35A", "Output PDO W05", "white"),
            ("Word 해석", "Write / Page 3 / Data\n세 필드를 분리해 판단", "Ethercat_Handle", "white"),
            ("원본 보관", "0xB35A 전체를\n채널별 I/O 저장소에 유지", "S7 · set_io_data", "gold_light"),
            ("RF 전송", "AP_IO_SET Payload에\n0xB35A 그대로 사용", "CMD=0x04", "white"),
            ("Peer 적용", "Page 3 값을 갱신하고\n응답 프레임 생성", "Peer firmware", "teal_light"),
            ("거래 종료", "응답 또는 timeout 후\nbusy/update 상태 정리", "S10 · transaction", "white"),
        ],
        accent="navy2",
        bit_data=(0xB35A, "W05 PAGE 3 WRITE", {15: PALETTE["warn_light"], 14: PALETTE["gold_light"], 13: PALETTE["gold_light"], 12: PALETTE["gold_light"], **{b: PALETTE["blue2"] for b in range(12)}}),
        note_text="같은 채널의 update_io가 남은 동안 새 set_io_data로 교체하지 않는다. WOS는 응답/상태 확인 후 다음 Write를 기록해야 한다.",
    )

    transaction_lane(
        img, 1740, "D", "SERIAL READ / WRITE", "W17 명령과 9 Word 요청 블록의 적재",
        [
            ("동반 I/O", "W13–W16 중\n대상 채널 1 Word", "staging[0]", "white"),
            ("명령", "W17\nWrite CH/Page\nRead CH/Page", "staging[1]", "white"),
            ("Serial Data", "W18–W24\n7 Word", "staging[2…8]", "gold_light"),
            ("RF 전송", "총 9 Word = 18 byte\nAP_SERIAL_SET/GET", "CMD=0x05/0x06", "white"),
            ("응답 적재", "동반 I/O → I/O cache\nSerial → Serial cache", "S5 + S6", "teal_light"),
            ("PDO 반환", "선택 Page의 8 Word\n→ W17–W24", "S2 · Input PDO", "white"),
        ],
        accent="steel",
        bit_data=(0x2300, "W17 SERIAL CH2 PAGE3 WRITE", {b: PALETTE["gold_light"] for b in range(8, 16)}),
        note_text="현재 수신 코드는 Serial 데이터를 항상 cache offset 0부터 기록하지만 Page 2…10 조회는 (Page−1)×8 offset을 읽는다. 다중 Page 갱신은 수정 전 보장하지 않는다.",
    )

    d = ImageDraw.Draw(img)
    d.line((70, 2248, 3530, 2248), fill=PALETTE["line"], width=3)
    text_left(img, (75, 2265), "Figure 2. 각 거래에서 값이 저장되는 시점과 저장 블록을 분리하였다. 괄호 속 기호는 코드 추적용이며 외부 공유용 블록명으로 사용하지 않는다.", pil_font(23), PALETTE["ink"])
    return img


def xlsx_spec(*, fill="white", color="ink", bold=False, size=12, align="center", valign="vcenter", border=1, wrap=True, italic=False, top=0, bottom=0, left=0, right=0):
    return {
        "fill": PALETTE.get(fill, fill), "color": PALETTE.get(color, color),
        "bold": bold, "size": size, "align": align, "valign": valign,
        "border": border, "wrap": wrap, "italic": italic,
        "top": top, "bottom": bottom, "left": left, "right": right,
    }


class PaperBook:
    def __init__(self, path):
        self.wb = xlsxwriter.Workbook(str(path), {"strings_to_urls": False, "strings_to_formulas": False})
        self.cache = {}
        self.run_cache = {}

    @staticmethod
    def default_font(value):
        if isinstance(value, str) and HANGUL_RE.search(value) and not LATIN_RE.search(value):
            return "맑은 고딕"
        return "Times New Roman"

    def fmt(self, s, value=None):
        font = self.default_font(value)
        key = tuple(s.values()) + (font,)
        if key not in self.cache:
            p = {
                "font_name": font, "font_size": s["size"], "font_color": s["color"],
                "bold": s["bold"], "italic": s["italic"], "bg_color": s["fill"],
                "pattern": 1, "align": s["align"], "valign": s["valign"], "text_wrap": s["wrap"],
            }
            if s["border"]:
                p.update({"border": s["border"], "border_color": PALETTE["line"]})
            for edge in ("top", "bottom", "left", "right"):
                if s[edge]:
                    p[edge] = s[edge]
                    p[f"{edge}_color"] = PALETTE["navy"]
            self.cache[key] = self.wb.add_format(p)
        return self.cache[key]

    def run_fmt(self, font, s):
        key = (font, s["size"], s["bold"], s["italic"], s["color"])
        if key not in self.run_cache:
            self.run_cache[key] = self.wb.add_format({
                "font_name": font, "font_size": s["size"], "font_color": s["color"],
                "bold": s["bold"], "italic": s["italic"],
            })
        return self.run_cache[key]

    def write(self, ws, row, col, value, s):
        f = self.fmt(s, value)
        if value is None:
            ws.write_blank(row, col, None, f)
            return
        if not isinstance(value, str):
            ws.write(row, col, value, f)
            return
        if not (HANGUL_RE.search(value) and LATIN_RE.search(value)):
            ws.write_string(row, col, value, f)
            return
        runs = []
        current = value[0]
        current_ko = bool(HANGUL_RE.match(value[0]))
        for ch in value[1:]:
            is_ko = bool(HANGUL_RE.match(ch))
            if is_ko == current_ko:
                current += ch
            else:
                runs.extend([self.run_fmt("맑은 고딕" if current_ko else "Times New Roman", s), current])
                current, current_ko = ch, is_ko
        runs.extend([self.run_fmt("맑은 고딕" if current_ko else "Times New Roman", s), current, f])
        ws.write_rich_string(row, col, *runs)

    def merge(self, ws, r1, c1, r2, c2, value, s):
        ws.merge_range(r1, c1, r2, c2, "", self.fmt(s, value))
        self.write(ws, r1, c1, value, s)


def sheet_title(doc, ws, number, english, korean, subtitle):
    doc.merge(ws, 0, 0, 0, 51, f"{number}. {english}", xlsx_spec(fill="navy", color="white", bold=True, size=16, align="left", border=0))
    doc.merge(ws, 1, 0, 1, 51, korean, xlsx_spec(fill="white", color="navy", bold=True, size=14, align="left", border=0, bottom=2))
    doc.merge(ws, 2, 0, 2, 51, subtitle, xlsx_spec(fill="white", color="slate", size=12, align="left", border=0))
    ws.set_row(0, 28)
    ws.set_row(1, 26)
    ws.set_row(2, 25)


def section_row(doc, ws, row, number, title_text):
    doc.merge(ws, row, 0, row, 51, f"{number}  {title_text}", xlsx_spec(fill="white", color="navy", bold=True, size=14, align="left", border=0, bottom=2))
    ws.set_row(row, 25)


def setup_sheet(ws, last_row, footer):
    ws.hide_gridlines(2)
    ws.set_zoom(70)
    ws.set_landscape()
    ws.set_paper(8)
    ws.fit_to_pages(1, 0)
    ws.set_margins(0.22, 0.22, 0.35, 0.35)
    ws.set_footer(f"&L{footer}&C&P / &N&RRev.2.00 | 2026-09-10")
    ws.print_area(0, 0, last_row, 51)
    ws.freeze_panes(3, 0)
    for c in range(52):
        ws.set_column(c, c, 3.05)


fig1 = make_architecture_figure()
fig2 = make_transaction_figure()
fig1.convert("RGB").save(PREVIEW_1, format="PNG", dpi=(220, 220), optimize=True)
fig2.convert("RGB").save(PREVIEW_2, format="PNG", dpi=(220, 220), optimize=True)
fig1_data = io.BytesIO()
fig2_data = io.BytesIO()
fig1.convert("RGB").save(fig1_data, format="PNG", dpi=(220, 220), optimize=True)
fig2.convert("RGB").save(fig2_data, format="PNG", dpi=(220, 220), optimize=True)
fig1_data.seek(0)
fig2_data.seek(0)

doc = PaperBook(BUILD)
wb = doc.wb
wb.set_properties({
    "title": "AP 중심 WOS–Peer 메모리 및 프로토콜 명세",
    "subject": "AP 내부 데이터 소유권, cache, request staging, FIFO, RF frame과 PDO 연계",
    "author": "Firmware Engineering",
    "comments": "AP source-based technical paper style workbook",
})


# Sheet 1: main architecture + exact memory table.
ws = wb.add_worksheet("01_AP메모리로드맵")
ws.set_tab_color(PALETTE["navy"])
setup_sheet(ws, 73, "01 AP 메모리 로드맵")
sheet_title(doc, ws, "1", "AP-CENTRIC DATA FLOW AND MEMORY OWNERSHIP", "AP 중심 데이터 흐름과 메모리 소유권", "주 도식은 데이터가 AP 안에서 실제로 어느 저장 블록을 거치는지에 초점을 둔다. PDO는 AP 진입·반환 경계로만 표시한다.")
for r in range(4, 35):
    ws.set_row(r, 18)
ws.insert_image(4, 0, "ap_memory_architecture.png", {"image_data": fig1_data, "x_scale": 0.405, "y_scale": 0.405, "x_offset": 5, "y_offset": 4, "object_position": 1, "description": "AP-centric WOS to Peer data flow and memory ownership diagram"})
doc.merge(ws, 35, 0, 35, 51, "Figure 1. 요청과 응답은 동일한 경로의 역방향이 아니다. 요청은 임시 저장소와 FIFO를 거치고, 응답은 검증 후 캐시에 영속되어 선택된 Word만 송신 PDO로 복사된다.", xlsx_spec(fill="white", color="ink", italic=True, size=12, align="left", border=0, top=1, bottom=1))

section_row(doc, ws, 37, "1.1", "AP 내부 저장 블록의 역할과 갱신 규칙")
headers = [
    (0, 2, "No."), (3, 12, "외부 공유용 블록명"), (13, 24, "저장 내용 / 크기"),
    (25, 34, "언제 기록되는가"), (35, 43, "누가 읽는가"), (44, 51, "코드 추적"),
]
for c1, c2, value in headers:
    doc.merge(ws, 38, c1, 38, c2, value, xlsx_spec(fill="navy", color="white", bold=True, size=12))
storage = [
    ("S1", "WOS 수신 PDO 이미지", "W00–W24 / 25 Word / 50 byte", "EtherCAT 전송 주기마다", "Word 해석기", "BufferOut.Cust"),
    ("S2", "WOS 송신 PDO 이미지", "W00–W24 / 25 Word / 50 byte", "Pairing·캐시 선택 결과 반영 시", "EtherCAT 전송", "BufferIn.Cust"),
    ("S3", "Pairing 요청 저장변수", "16 bit 채널 bitmap", "W00을 읽을 때", "Pairing 판단부", "tx_pairStatus"),
    ("S4", "채널 식별·기능표", "16채널의 MAC/Type/ID/paired/Page/Serial", "Pairing 응답 수신 시", "스케줄러·응답 검증", "peer[16]"),
    ("S5", "채널별 I/O 응답 캐시", "16 × 7 Word; index0 또는 Page1…6", "I/O 응답 검증 통과 시", "PDO 캐시 선택기", "lPeerData[16][7]"),
    ("S6", "Serial 페이지 캐시", "5 × 81 Word 할당; 4채널 사용", "Serial 응답 검증 통과 시", "Serial Page 선택기", "lSPeerData[5][81]"),
    ("S7", "채널별 요청 임시 저장소", "I/O 1 Word / Serial 9 Word", "WOS 요청을 채택할 때", "RF 프레임 생성부", "set_io_data / set_serial_Buf"),
    ("S8", "송신 대기 FIFO", "채널 번호만 저장; 49 entry 사용", "송신 예약 시", "RF 송신 스케줄러", "wifi_send.queue"),
    ("S9", "채널별 RF 프레임 버퍼", "Header 6 + Payload + CRC 2; 최대 50 byte", "송신 직전", "ESP-NOW 송신부", "txBuf[channel]"),
    ("S10", "거래 상태 플래그", "TX/RX busy, request/response, timeout", "상태 전환마다", "스케줄러·callback", "tx_busy / rx_busy / peer_req"),
]
spans = [(0, 2), (3, 12), (13, 24), (25, 34), (35, 43), (44, 51)]
for r, row_data in enumerate(storage, start=39):
    fill = "white" if r % 2 else "gray"
    for (c1, c2), value in zip(spans, row_data):
        doc.merge(ws, r, c1, r, c2, value, xlsx_spec(fill=fill, bold=c1 in (0, 3), size=11, align="left" if c1 else "center"))
    ws.set_row(r, 33)

doc.merge(ws, 49, 0, 50, 51, "해석 기준: ‘캐시’는 단순 표시용 배열이 아니라 Peer 응답의 현재 AP 소유본이다. WOS는 RF 응답을 직접 읽지 않고 AP 송신 PDO에 복사된 선택 Word만 읽는다. 반대로 FIFO는 데이터 저장소가 아니며, 송신할 채널 번호만 보관한다.", xlsx_spec(fill="gold_light", color="ink", bold=True, size=12, align="left", border=1))

section_row(doc, ws, 52, "1.2", "코드 추적 근거 — 일반 블록명과 실제 소스의 연결")
trace_headers = [(0, 11, "검증 대상"), (12, 24, "핵심 동작"), (25, 39, "현재 소스 위치"), (40, 51, "문서 해석")]
for c1, c2, value in trace_headers:
    doc.merge(ws, 53, c1, 53, c2, value, xlsx_spec(fill="navy2", color="white", bold=True))
trace_rows = [
    ("PDO 경계", "수신/송신 Word 포인터 연결", "src/ApToEthercat_V4.0_260528.ino:467–472", "S1·S2의 실제 접근 지점"),
    ("Page Word 해석", "bit15 R/W, bit14…12 Page", "동일 파일:637–674", "원본 16 bit를 S7에 저장"),
    ("Serial 9 Word 적재", "동반 I/O + W17–W24 복사", "동일 파일:680–737", "S7의 Serial 저장 구조"),
    ("I/O 응답 캐시", "Payload를 index0 또는 index1부터 복사", "동일 파일:1025–1057, 1085–1146", "Page tag 재정렬 없음"),
    ("Serial 응답 캐시", "Serial 영역을 offset0부터 복사", "동일 파일:1160–1209", "Page2…10 조회 offset과 불일치"),
    ("RF Payload 생성", "I/O 1 Word / Serial 9 Word", "동일 파일:1692–1748", "S7 → S9 변환"),
    ("채널 순환", "호출당 채널 1개, modulo 16", "동일 파일:446, 743", "한 호출이 전 채널을 처리하지 않음"),
]
for r, row_data in enumerate(trace_rows, start=54):
    for (c1, c2), value in zip([(0, 11), (12, 24), (25, 39), (40, 51)], row_data):
        doc.merge(ws, r, c1, r, c2, value, xlsx_spec(fill="white" if r % 2 else "gray", size=11, align="left", bold=c1 == 0))
    ws.set_row(r, 32)

section_row(doc, ws, 62, "1.3", "운용자가 이해해야 하는 시간·상태 제약")
notes = [
    (0, 16, "채널 검사 주기", "Ethercat_Handle() 한 번에 채널 1개만 처리한다. 현재 ++cnt_ms10 > 10 조건은 11개의 서비스 tick마다 호출되고 16채널 1회전은 계산상 176 service tick이다. 이는 hard real-time 최대시간 측정값이 아니다."),
    (17, 33, "연속 Write", "같은 채널의 update_io가 남아 있으면 새 요청 Word를 임시 저장소로 옮기지 않는다. 중간 명령 유실을 피하려면 WOS는 응답 또는 상태 변화를 확인한 뒤 다음 값을 쓴다."),
    (34, 51, "응답과 Pairing 상태", "Pairing 응답만으로 W00 완료 bit를 즉시 올리지 않는다. 이후 유효한 I/O 또는 Serial 응답이 확인되어야 동일 채널 상태 bit가 설정된다."),
]
for c1, c2, head, body in notes:
    doc.merge(ws, 63, c1, 63, c2, head, xlsx_spec(fill="blue2", color="navy", bold=True, border=1))
    doc.merge(ws, 64, c1, 68, c2, body, xlsx_spec(fill="white", align="left", size=11, border=1))
doc.merge(ws, 70, 0, 72, 51, "범위: 본 시트는 AP 코드에서 직접 확인 가능한 메모리 이동과 상태 전환을 기술한다. 장치 단위·범위는 제공된 PDO 캡처 전사값이며, 실제 Peer firmware와의 일치 여부는 장치 담당자가 별도로 승인해야 한다.", xlsx_spec(fill="gray", color="slate", italic=True, align="left", border=0, top=1))


# Sheet 2: transaction diagram + compact protocol/device reference.
ws = wb.add_worksheet("02_거래별적재해설")
ws.set_tab_color(PALETTE["teal"])
setup_sheet(ws, 102, "02 거래별 적재 해설")
sheet_title(doc, ws, "2", "WORD-TO-MEMORY TRANSACTION LIFECYCLE", "거래별 Word–메모리 적재 해설", "Pairing, I/O Read, I/O Write, Serial 거래에서 값이 저장되는 위치와 시점을 실제 Word 예시로 추적한다.")
for r in range(4, 46):
    ws.set_row(r, 18)
ws.insert_image(4, 0, "transaction_lifecycle.png", {"image_data": fig2_data, "x_scale": 0.405, "y_scale": 0.405, "x_offset": 5, "y_offset": 4, "object_position": 1, "description": "Pairing, paged I/O and Serial word to memory lifecycle diagram"})
doc.merge(ws, 46, 0, 46, 51, "Figure 2. Word 값, AP 내부 저장 블록, RF 명령, 응답 캐시와 PDO 반환 지점을 동일 거래선에서 비교한다.", xlsx_spec(fill="white", color="ink", italic=True, size=12, align="left", border=0, top=1, bottom=1))

section_row(doc, ws, 48, "2.1", "PDO Word 그룹과 AP 내부 연결점")
group_headers = [(0, 5, "Word"), (6, 18, "WOS → AP"), (19, 31, "AP 내부 연결점"), (32, 44, "AP → WOS"), (45, 51, "근거")]
for c1, c2, value in group_headers:
    doc.merge(ws, 49, c1, 49, c2, value, xlsx_spec(fill="navy", color="white", bold=True))
pdo_groups = [
    ("W00", "Pairing 요청 bitmap", "16 bit Pairing 요청 저장변수 + 채널 기능표", "Pairing 확인 bitmap", "Code"),
    ("W01–W12", "일반 I/O 채널 CH01–CH12", "채널별 I/O 임시 저장소 / I/O 응답 캐시", "채널별 I/O 응답", "Code"),
    ("W13–W16", "Serial Peer CH13–CH16 동반 I/O", "Serial 9 Word staging[0] / I/O 응답 캐시", "동반 I/O 응답", "Code"),
    ("W17", "Write CH/Page + Read CH/Page", "Serial staging[1] / Page cache selector", "선택 Serial Page 상태", "Code"),
    ("W18–W24", "Serial Data 7 Word", "Serial staging[2…8] / Serial Page cache", "Serial Data 7 Word", "Code"),
]
for r, row_data in enumerate(pdo_groups, start=50):
    for (c1, c2), value in zip([(0, 5), (6, 18), (19, 31), (32, 44), (45, 51)], row_data):
        doc.merge(ws, r, c1, r, c2, value, xlsx_spec(fill="white" if r % 2 else "gray", size=11, align="left" if c1 not in (0, 45) else "center", bold=c1 == 0))
    ws.set_row(r, 34)
doc.merge(ws, 55, 0, 55, 51, "대표 Word 예시: Pairing CH05는 W00=0x0010과 W05=0x0233, Page 2 Read는 W05=0x2000, Page 3 Write는 W05=0xB35A, Serial CH2 Page 3 Write는 W17=0x2300이다.", xlsx_spec(fill="gold_light", color="ink", bold=True, align="left", size=11))

section_row(doc, ws, 56, "2.2", "RF Frame과 명령별 Payload")
rf_fields = [
    (0, 5, "Byte 0\nGroup"), (6, 11, "Byte 1\nChannel"), (12, 17, "Byte 2\nCommand"),
    (18, 23, "Byte 3\nType"), (24, 29, "Byte 4\nAddress"), (30, 35, "Byte 5\nTotal Length"),
    (36, 45, "Byte 6…N−3\nPayload"), (46, 51, "Byte N−2…N−1\nCRC16"),
]
for c1, c2, value in rf_fields:
    doc.merge(ws, 57, c1, 59, c2, value, xlsx_spec(fill="blue2", color="navy", bold=True, size=11, border=1))
cmds = [
    ("0x01 / 0x81", "Pairing 요청 / 응답", "기능정보 3 byte 이상"),
    ("0x02 / 0x82", "Pairing 해제 요청 / 응답", "명령별"),
    ("0x03 / 0x83", "I/O GET 요청 / 응답", "요청 1 Word"),
    ("0x04 / 0x84", "I/O SET 요청 / 응답", "요청 1 Word"),
    ("0x05 / 0x85", "Serial SET 요청 / 응답", "요청 9 Word"),
    ("0x06 / 0x86", "Serial GET 요청 / 응답", "요청 9 Word"),
]
for i, (code, meaning, payload) in enumerate(cmds):
    row = 61 + i // 3
    start = (i % 3) * 17
    end = start + 16 if i % 3 < 2 else 51
    doc.merge(ws, row, start, row, start + 5, code, xlsx_spec(fill="navy2", color="white", bold=True, size=11))
    doc.merge(ws, row, start + 6, row, end - 4, meaning, xlsx_spec(fill="white", align="left", size=11))
    doc.merge(ws, row, end - 3, row, end, payload, xlsx_spec(fill="gray", size=10))
    ws.set_row(row, 31)
doc.merge(ws, 64, 0, 65, 51, "CRC16-MODBUS, 초기값 0xFFFF. CRC 두 byte의 실제 무선 전송 순서는 C++ bit-field 배치에 의존하므로 타 MCU 이식이나 외부 분석기 구현 시 캡처로 Low/High 순서를 확인한다.", xlsx_spec(fill="gold_light", color="warn", bold=True, align="left", size=11))

section_row(doc, ws, 67, "2.3", "장치별 DI·DO·SI·SO 의미 — 제공된 PDO 캡처 전사")
device_headers = [(0, 3, "Type"), (4, 8, "장치"), (9, 20, "DI · AP→WOS"), (21, 31, "DO · WOS→AP"), (32, 41, "SI · AP→WOS"), (42, 51, "SO · WOS→AP")]
for c1, c2, value in device_headers:
    doc.merge(ws, 68, c1, 69, c2, value, xlsx_spec(fill="navy", color="white", bold=True, size=11))
device_rows = [
    ("0x01", "DIW", "P1 Flow Data[lpm]\nP2 Press Data[Pa]", "-", "-", "-", 52),
    ("0x02", "CDA", "P1 Flow Data[lpm]\nP2 Press Data[Pa]", "-", "-", "-", 52),
    ("0x03", "S-Damper", "-", "-", "P1 L18 Mode; L19 Target Pressure[Pa]; L20 Hys[Pa]; L21 Valve Angle[°]; L22 Pressure Rate[Pa]; L23 Alarm", "P1 L18 Init Mode; L19 Target Pressure 1…1024Pa; L20 Hys 1…100Pa; L21 Angle 0…90°", 92),
    ("0x04", "X-Ray", "P1* ID1 bits0…4 / ID2 bits5…9 / ID3 bits10…14\nMode, Interlock, Power, OverTime, Alarm", "P1* ID1 bits0…1 / ID2 bits2…3 / ID3 bits4…5\nMode, Interlock", "P1…n Tube LifeTime: ID1 L18…19, ID2 L20…21, ID3 L22…23(추정)", "-", 92),
    ("0x05", "D40A", "Door State bit0…5\n0 Open / 1 Close", "-", "-", "-", 52),
    ("0x06", "D4SL", "bit2n Door Open/Close\nbit2n+1 Lock/Unlock", "bit0…7 Lock/Unlock\nbit8 Lock All", "-", "-", 62),
    ("0x07", "TIC", "-", "-", "P1…n L18 Current; L19 Target; L20 P; L21 I; L22 D; L23 AL2", "P1…n L18 Target 10…50℃; L19 AL1 0…100℃; L20 P 1…10; L21 I 1…20; L22 D 1…30; L23 AL2 0…100℃", 92),
    ("0x08", "IFC", "P1 Flow[lpm]; P2 Press[bar]; P4 Open Rate[%]; P6 Status bits0,1…2,3…4,5…10", "P3 Open Rate 0…100%; Flow 0…???lpm\nP5 Target Mode bits3…4", "-", "-", 78),
    ("0x09", "Manometer", "P1 Exhaust Data[Pa]", "-", "-", "-", 48),
    ("0x0A", "US Level SS", "P1 Level Data[mm]", "-", "-", "-", 48),
]
row = 70
for code, device, di, do, si, so, height in device_rows:
    values = [(0, 3, code), (4, 8, device), (9, 20, di), (21, 31, do), (32, 41, si), (42, 51, so)]
    for c1, c2, value in values:
        doc.merge(ws, row, c1, row, c2, value, xlsx_spec(fill="white" if row % 2 else "gray", bold=c1 in (0, 4), size=10, align="left" if c1 >= 9 else "center"))
    ws.set_row(row, height)
    row += 1

section_row(doc, ws, 81, "2.4", "미정·제약 항목")
open_items = [
    (0, 16, "TBD-01 · S-Damper", "Mode 2 명칭이 캡처에서 잘려 있으므로 원본 장치 명세 확인 필요"),
    (17, 33, "TBD-02 · IFC", "Auto Target Flow의 상한이 0…???로 표시되어 원본 범위 확인 필요"),
    (34, 51, "TBD-03 · X-Ray", "Tube LifeTime의 2-Line 배치는 캡처 병합 경계를 기반으로 한 추정"),
]
for c1, c2, head, body in open_items:
    doc.merge(ws, 82, c1, 82, c2, head, xlsx_spec(fill="warn_light", color="warn", bold=True))
    doc.merge(ws, 83, c1, 85, c2, body, xlsx_spec(fill="white", color="ink", align="left", size=11))
doc.merge(ws, 87, 0, 89, 51, "Serial 구현 제약: 응답 수신부는 Serial 데이터를 캐시 offset 0부터 기록한다. 반면 WOS Read 선택부는 Page 2…10에서 (Page−1)×8 offset을 읽는다. 따라서 현재 소스만으로는 Page 2…10의 최신 응답 반영을 보장할 수 없다.", xlsx_spec(fill="warn_light", color="warn", bold=True, align="left", border=2, size=12))
doc.merge(ws, 91, 0, 93, 51, "Page 응답 구현 제약: AP는 Peer 응답에 포함된 각 Word의 Page 번호를 해석해 해당 index로 재배치하지 않는다. 응답 Payload의 첫 Word를 cache[1], 다음 Word를 cache[2]에 연속 복사한다. 누락 Page가 있으면 이후 Page가 당겨져 저장된다.", xlsx_spec(fill="gold_light", color="ink", bold=True, align="left", border=2, size=12))
doc.merge(ws, 95, 0, 98, 51, "명칭 주석: 제공 캡처의 Type 0x08 명칭은 IFC이고 AP 내부 소스 기호는 LMFC다. Type 0x0A는 캡처에서 US Level SS, 내부 기호는 LCT다. 외부 공유 문서에는 캡처 명칭을 우선하고 코드 추적 시에만 내부 기호를 사용한다.", xlsx_spec(fill="gray", color="slate", italic=True, align="left", border=0, top=1, size=11))

wb.close()


with zipfile.ZipFile(BUILD, "r") as archive:
    bad = archive.testzip()
    if bad:
        raise RuntimeError(f"Invalid XLSX member: {bad}")
    ns = {"m": "http://schemas.openxmlformats.org/spreadsheetml/2006/main"}
    workbook_xml = ET.fromstring(archive.read("xl/workbook.xml"))
    sheet_names = [n.attrib["name"] for n in workbook_xml.findall("m:sheets/m:sheet", ns)]
    expected = ["01_AP메모리로드맵", "02_거래별적재해설"]
    if sheet_names != expected:
        raise RuntimeError(f"Unexpected sheets: {sheet_names}")
    media = [name for name in archive.namelist() if name.startswith("xl/media/")]
    if len(media) != 2:
        raise RuntimeError(f"Expected two embedded figures, found {media}")
    style_xml = archive.read("xl/styles.xml").decode("utf-8", errors="replace")
    for font_name in ("Times New Roman", "맑은 고딕"):
        if font_name not in style_xml:
            raise RuntimeError(f"Missing font: {font_name}")
    shared = ET.fromstring(archive.read("xl/sharedStrings.xml"))
    visible = "\n".join("".join(si.itertext()) for si in shared.findall("m:si", ns))
    for required in ("AP 내부 저장 블록", "FIFO는 데이터 저장소", "0xB35A", "lPeerData[16][7]", "Serial 구현 제약"):
        if required not in visible:
            raise RuntimeError(f"Missing required content: {required}")

digest = hashlib.sha256(BUILD.read_bytes()).hexdigest()
os.replace(BUILD, OUT)
print(f"VALIDATED_XLSX|sheets=2|figures=2|sha256={digest}")
print(f"CREATED|{OUT}|{OUT.stat().st_size}")
print(f"PREVIEW|{PREVIEW_1}|{PREVIEW_1.stat().st_size}")
print(f"PREVIEW|{PREVIEW_2}|{PREVIEW_2.stat().st_size}")
