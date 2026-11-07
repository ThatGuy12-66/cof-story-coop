"""labels.json, every other image make_art.py repaints, block by block: B(box, russian, chinese, style=..., ...)
(see make_art.py's draw_block and STYLES for the keys).

    python labels_spec.py           (writes labels.json beside it)
"""
from pathlib import Path

V = "overlay/gfx/vgui/"
L = {}


def B(box, ru, zh, **kw):
    return dict(box=box, russian=ru, schinese=zh, **kw)


# ---------------------------------------------------------------------------------------------- game over
for name, ru, zh in (("game_over.tga", "ИГРА ОКОНЧЕНА", "游戏结束"), ("game_over_exit.tga", "ВЫХОД", "退出"),
                     ("game_over_exit2.tga", "ВЫХОД", "退出"), ("game_over_load.tga", "ЗАГРУЗИТЬ ИГРУ", "读取游戏"),
                     ("game_over_load2.tga", "ЗАГРУЗИТЬ ИГРУ", "读取游戏")):
    red = name.endswith("2.tga")
    w, h = (457, 295) if name == "game_over.tga" else (300, 50)
    L[V + name] = {"blocks": [B([0, 0, w, h], ru, zh, style="streak", maxw=w - 30,
                                **({"color": [156, 0, 0]} if red else {}))]}

# ---------------------------------------------------------------------------------------------- the doctor's bar
for name in ("doctor_health_empty.tga", "doctor_health_full.tga"):
    L[V + "boss/" + name] = {"blocks": [B([0, 33, 200, 12], "Доктор", "医生", style="plain", maxw=180, scale_cjk=1.3)]}

# ---------------------------------------------------------------------------------------------- yes / no dialogs
YES = B([38, 151, 75, 38], "ДА", "是", style="dialog", remove=["color", [255, 255, 255], 0.502], maxw=64)
NO = B([207, 151, 75, 38], "НЕТ", "否", style="dialog", remove=["color", [255, 255, 255], 0.502], maxw=64)
for name, ru, zh in (("640_roofboss.TGA", ["ХОЧЕШЬ", "СБЕЖАТЬ?"], "你想逃走吗？"),
                     ("640_savegame.tga", ["СОХРАНИТЬ", "ПРОГРЕСС?"], "要保存进度吗？"),
                     ("640_weapongive.tga", ["ОТДАТЬ ЕМУ", "ОРУЖИЕ?"], "要把武器交给他吗？")):
    L[V + name] = {"blocks": [B([0, 30, 320, 85], ru, zh, style="dialog", size=22, center=[160, 73.5], maxw=296),
                              YES, NO]}

# ---------------------------------------------------------------------------------------------- cancel buttons, titles
L[V + "640_cancelbutton.tga"] = {"blocks": [B([8, 15, 87, 23], "ОТМЕНА", "取消", style="engraved", remove="polish",
                                              size=15, center=[51, 26], maxw=84, hole=12, loud=15)]}
L[V + "save_load.tga"] = {"blocks": [
    B([128, 24, 388, 46], "СОХРАНЁННЫЕ ИГРЫ", "已保存的游戏", style="carved", remove="polish", size=33,
      center=[321, 48], maxw=420, hole=14, loud=18),
    B([265, 407, 110, 26], "ОТМЕНА", "取消", style="carved", remove="polish", size=15, center=[320, 420.5], maxw=96, hole=14, loud=18,
      font={"russian": "sans_bold", "schinese": "cjk_bold"})]}
L[V + "puzzle/padlock_buttons.tga"] = {"blocks": [
    B([128, 87, 44, 12], "ОТМЕНА", "取消", style="plain_bold", remove="polish", size=7, center=[150, 93], maxw=44,
      color=[75, 8, 8])]}
L[V + "unlockables/unlockables_menu.tga"] = {"blocks": [
    B([216, 45, 368, 43], "ОТКРЫВАЕМОЕ", "解锁内容", style="carved", remove="polish", size=34, center=[400, 66],
      maxw=440, hole=14, loud=18),
    B([320, 518, 160, 23], "ГЛАВНОЕ МЕНЮ", "主菜单", style="plain_bold", remove="polish", color=[140, 0, 0],
      maxw=170, scale_cjk=1.2)]}
L[V + "lobby_background.tga"] = {"blocks": [
    B([140, 58, 520, 44], "КООПЕРАТИВНОЕ ЛОББИ", "合作大厅", style="streak", remove="polish", maxw=560,
      ink=[215, 210, 205], streak=(30, 0.6))]}
L["overlay/models/page_locked.tga"] = {"blocks": [
    B([16, 204, 316, 82], "ЗАКРЫТО", "未解锁", style="sketch", remove="polish", size=46, center=[173, 245],
      maxw=300)]}
L["models/developer_commentary/dev_text_fullbright.bmp"] = {"blocks": [
    B([0, 80, 256, 96], ["КОММЕНТАРИИ", "РАЗРАБОТЧИКОВ"], ["开发者", "评论"], style="stencil",
      remove=["color", [0, 0, 0], 1.0], size=34, center=[128, 127.5], maxw=244)]}
L[V + "telescope_scope.tga"] = {"blocks": [
    B([100, 497, 312, 15], "Осматривайтесь мышью; «Использовать» или «Атака» — отмена",
      "用鼠标环顾四周，按“使用”或“攻击”取消", style="plain", remove=["color", [0, 0, 0], 1.0], maxw=480,
      center=[256, 505])]}

# ---------------------------------------------------------------------------------------------- inventory buttons
ITEM_ROWS = {"inventory_items.tga": (("ПРИМЕНИТЬ", "使用"), ("ОБЪЕДИНИТЬ", "组合"), ("ВЫБРОСИТЬ", "丢弃")),
             "inventory_weapons.tga": (("ЭКИПИРОВАТЬ", "装备"), ("ОБЪЕДИНИТЬ", "组合"), ("ВЫБРОСИТЬ", "丢弃"),
                                       ("ДВЕ РУКИ", "双持"))}
for name, rows in ITEM_ROWS.items():
    L[V + name] = {"blocks": [B([3, 42 * i + 5, 120, 32], ru, zh, style="streak", remove="inpaint", radius=6,
                                maxw=104, streak=(20, 0.8), scale=0.86) for i, (ru, zh) in enumerate(rows)]}

# ---------------------------------------------------------------------------------------------- the inventory screens
def inv(boxes):
    out = []
    for ink, ru, zh, how, *maxw in boxes:
        x, y, w, h = ink
        big = h > 15
        box = [x - 14, y - 5, w + 40, h + 10] if big else [x - 15, y - 4, w + 30, h + 8]
        out.append(B(box, ru, zh, style="streak", remove=how, align="left", left=x,
                     maxw=(maxw[0] if maxw else 175 if big else 200), streak=((20, 0.7) if big else (12, 0.6)),
                     radius=5, **({} if big else {"scale_cjk": 1.25})))
    return out


SLOTS = lambda y: [([403, y, 32, 9], "СЛОТ 1", "快捷栏 1", "inpaint"), ([505, y, 34, 9], "СЛОТ 2", "快捷栏 2", "inpaint"),
                   ([608, y, 32, 9], "СЛОТ 3", "快捷栏 3", "inpaint")]
DESC = lambda y: [([37, y, 90, 9], "ОПИСАНИЕ ПРЕДМЕТА", "物品描述", "inpaint"),
                  ([404, y, 102, 9], "ТЕКУЩАЯ ЦЕЛЬ", "当前目标", "inpaint")]
QUICK = ("БЫСТРЫЙ ВЫБОР", "快捷选择")
L[V + "640_inventory.tga"] = {"blocks": inv([
    ([36, 35, 50, 22], "СУМКА", "背包", ["patch", 130, 0]),
    ([402, 35, 179, 23], *QUICK, ["patch", -260, 0], 190),
    ([37, 195, 115, 22], "КАРМАНЫ", "口袋", ["patch", 170, 0]),
    ([404, 242, 80, 22], "ЗАПИСКИ", "笔记", "inpaint", 112)] + SLOTS(181) + DESC(337))}
L[V + "640_inventory_three.tga"] = {"blocks": inv([
    ([37, 34, 115, 22], "КАРМАНЫ", "口袋", ["patch", 170, 0]),
    ([402, 35, 179, 23], *QUICK, ["patch", -260, 0], 190),
    ([404, 242, 80, 22], "ЗАПИСКИ", "笔记", "inpaint", 112)] + SLOTS(181) + DESC(337))}
for name in ("640_inventory_p.TGA", "640_inventory_p_c.tga"):
    blocks = inv([([38, 52, 111, 22], "ФОРМА", "制服", ["patch", 170, 0]),
                  ([402, 52, 179, 23], *QUICK, ["patch", 0, 170], 190)] + SLOTS(198) + DESC(318))
    if name == "640_inventory_p.TGA":
        blocks.append(B([406, 340, 190, 18], "Найди Саймона и спаси его.", "找到西蒙，把他救出来。", style="plain",
                        remove="inpaint", align="left", left=414, maxw=170, radius=5))
    L[V + name] = {"blocks": blocks}

# ---------------------------------------------------------------------------------------------- note list labels
NOTES = {"codefrompc.tga": ("Код с компьютера:", "电脑上的密码："),
         "codefromvideotape.tga": ("Код с видеокассеты:", "录像带上的密码："),
         "codefromwindow.tga": ("Код на окне:", "窗户上的密码："),
         "hiddennote1.TGA": ("Спрятанная записка №1", "藏起来的纸条 #1"),
         "hiddennote2.TGA": ("Спрятанная записка №2", "藏起来的纸条 #2"),
         "mylife.tga": ("Моя жизнь", "我的人生"),
         "mysecretnote1.tga": ("Моя тайная записка №1", "我的秘密纸条 #1"),
         "mysecretnote2.tga": ("Моя тайная записка №2", "我的秘密纸条 #2"),
         "mysecretnote3.tga": ("Моя тайная записка №3", "我的秘密纸条 #3"),
         "mysecretnote4.tga": ("Моя тайная записка №4", "我的秘密纸条 #4"),
         "personalnote.tga": ("Личная записка", "私人纸条"),
         "phonecode.TGA": ("Код с телефона:", "手机上的密码："),
         "purnellnote.tga": ("Записка Пёрнелла", "珀内尔的纸条"),
         "rippedmagarticle.tga": ("Вырванная статья из журнала", "撕下的杂志文章")}
for name, (ru, zh) in NOTES.items():
    wide = name not in ("codefrompc.tga", "codefromvideotape.tga", "codefromwindow.tga", "phonecode.TGA")
    L[V + "notes/" + name] = {"blocks": [B([0, 0, 640 if wide else 300, 30], ru, zh, style="pencil",
                                           ink=[60, 60, 60], tol=120, maxw=(560 if wide else 280), scale_cjk=0.8,
                                           color=[30, 30, 32], rough=0.2,
                                           **({"align": "left"} if wide else {}))]}

# ---------------------------------------------------------------------------------------------- introductions
INTRO = {
    "english_sp.TGA": (
        ["ВСТУПЛЕНИЕ", "", "«Я всю жизнь чувствовал себя одиноким,", "сколько себя помню.",
         "Не знаю, нравится ли мне это... или я просто", "привык, но одно я знаю точно:",
         "одиночество меняет тебя, а когда тебе всё время паршиво, горько",
         "и злобно... это просто съедает тебя изнутри.»"],
        ["序言", "", "“我这辈子一直都觉得孤独，", "从我记事起就是这样。", "我不知道自己是喜欢这样……还是只是习惯了，",
         "但我知道一件事：", "孤独会改变一个人，一直觉得糟透了、满心苦涩、", "满腔怒火……这一切只会把你一点点吞噬。”"]),
    "english_coop.TGA": (
        ["ВСТУПЛЕНИЕ", "", "«Мы прочли книгу, книгу Саймона. Мы думали, что если читать его слова и пытаться жить "
         "и чувствовать так, как чувствовал он... может, это поможет нам понять, что с ним случилось. Но это нас не "
         "только ужаснуло: мы застряли в его мире, в ловушке его слов и страхов... Мы решили, что остановить это можно, "
         "лишь спасши его. Спасти от того, что изначально погрузило его во тьму безумия. Мы хотели увидеть, что "
         "будет, если попытаться его спасти, и именно это нам и нужно сделать. Мы должны найти его и спасти.", "",
         "Это часть нашего расследования...", "Наш взгляд на книгу Саймона.»"],
        ["序言", "", "“我们读了那本书，西蒙的书。我们以为读他的文字，试着像他那样生活、像他那样感受……也许能让我们看清他身上"
         "发生的事。然而，这不仅让我们恐惧，我们还被困在了他的世界里，被他的文字和焦虑困住……我们觉得，唯一能阻止这一切的"
         "办法就是救他。把他从最初让他坠入黑暗疯狂的东西中救出来。我们想看看，如果我们试着救他会发生什么，而这正是我们必须"
         "做的。我们必须找到他，把他救出来。", "", "这是我们调查的一部分……", "我们对西蒙之书的看法。”"]),
    "english_doc.TGA": (
        ["ВСТУПЛЕНИЕ", "", "«Стало ясно, что состояние Саймона не улучшается, как я надеялся. Я предложил Саймону "
         "вести книгу и записывать в неё свои чувства и тревоги, когда ему это нужно. Однако, похоже, это вышло "
         "из-под контроля, и на сеансах терапии стало ясно, что книга оказала лишь негативное и пугающее "
         "воздействие. Я больше не могу помочь Саймону. Нет такого лечения, которое изменило бы его разум. "
         "Остаётся лишь надеяться остановить его расстройство, прежде чем он навредит себе или окружающим. "
         "Я должен найти книгу. Я должен её уничтожить.»"],
        ["序言", "", "“很明显，西蒙的精神状况并没有像我当初期望的那样好转。我曾建议西蒙记一本书，在需要的时候写下自己的感受"
         "和焦虑。然而这件事似乎已经失控了，在治疗过程中我越来越清楚，这本书只带来了负面而可怕的影响。我再也帮不了西蒙了。"
         "我没有任何治疗方法能改变他的心智。我只能希望在他伤害自己或身边的人之前，阻止他的精神障碍。我必须找到那本书。"
         "我必须毁掉它。”"]),
}
for name, (ru, zh) in INTRO.items():
    L[V + "introductions/english/" + name] = {"blocks": [
        B([0, 0, 512, 512], ru, zh, style="streak", size=12, center=[256, 256], maxw=470, maxh=480, wrap=True,
          streak=(10, 0.5), pitch=21.6)]}

# ---------------------------------------------------------------------------------------------- the phone's screen
# Every phone texture has the screen in the same place: header 66..77, text from 95 every 17, soft keys at 220.
PHONE_FONT = {"russian": "mono_bold", "schinese": "cjk_bold"}
INK = [38, 42, 46]


def phone(header, body):
    blocks = [B([42, 214, 66, 21], "Опции", "选项", style="plain", font=PHONE_FONT, color=INK, remove="inpaint_ink",
                size=8, align="left", left=44, center=[0, 225.5], maxw=64, radius=3, scale_cjk=1.4),
              B([128, 214, 64, 21], "Назад", "返回", style="plain", font=PHONE_FONT, color=INK, remove="inpaint_ink",
                size=8, align="right", right=187, center=[0, 225.5], maxw=60, radius=3, scale_cjk=1.4)]
    if header:
        blocks.append(B([60, 64, 112, 16], *header, style="plain", font=PHONE_FONT, color=INK, remove="inpaint_ink",
                        size=8, center=[116.5, 71.5], maxw=110, radius=3, scale_cjk=1.4))
    if body:
        blocks.append(B([41, 91, 152, 114], *body, style="plain", font=PHONE_FONT, color=INK, remove="inpaint_ink",
                        size=8, align="left", left=43, first_center=100.5, maxw=146, wrap=True, pitch=17,
                        radius=3, scale_cjk=1.4))
    return {"blocks": blocks}


MOM = ("Мама", "妈妈")
MESS1 = ("Где ты? Иди домой как можно скорее, уже темнеет.", "你在哪儿？尽快回家，天快黑了。")
MESS2 = ("Посмотри направо.", "往右边看。")
M = "models/weapons/mobile/v_mobile/"
L[M + "fullbright_mess1.bmp"] = phone(MOM, MESS1)
L[M + "tex1.bmp"] = phone(MOM, MESS1)
L[M + "fullbright_mess2.bmp"] = phone(None, MESS2)
L[M + "fullbright_mess3.bmp"] = phone(None, ("Помоги мне!! Пожалуйста, помоги!! Они идут! Я в жилом доме, скорее!!",
                                             "救救我！！求你了，救命！！它们来了！我在公寓楼里，快点！！"))
L[M + "fullbright_mess4.bmp"] = phone(None, ("Четвёртый этаж!!", "四楼！！"))
L[M + "fullbright_mess5.bmp"] = phone(None, ("Я здесь.", "在这里。"))
L[M + "fullbright_mess7.bmp"] = phone(MOM, ("С тобой всё в порядке? Лучше иди домой, я начинаю волноваться.",
                                            "你还好吗？快回家吧，我开始担心了。"))
L[M + "fullbright_lowbattery.bmp"] = {"blocks": [
    B([60, 112, 112, 17], "Внимание", "警告", style="plain", font=PHONE_FONT, color=[70, 75, 80], remove="inpaint_ink",
      size=8, center=[115.5, 120.5], maxw=140, radius=3, scale_cjk=1.4),
    B([45, 162, 142, 17], "Батарея разряжена", "电量不足", style="plain", font=PHONE_FONT, color=[70, 75, 80],
      remove="inpaint_ink", size=8, center=[115.5, 170.5], maxw=140, radius=3, scale_cjk=1.4)]}
for rel in ("models/cutscene/mobileprop/fullbright_mess2.bmp", "models/cutscene/player3/mess2.bmp",
            "models/cutscene/player4/mess2.bmp"):
    L[rel] = phone(None, MESS2)
for rel in ("models/cutscene/mobileprop/tex1.bmp", "models/cutscene/player3/tex1.bmp",
            "models/collab/intro_customphone/tex1.bmp"):
    L[rel] = phone(MOM, MESS1)

# ---------------------------------------------------------------------------------------------- world: blood and paint
T = "textures/"


def pad(box, n=4, size=None):
    x, y, w, h = box
    W, H = size or (4096, 4096)
    x0, y0 = max(0, x - n), max(0, y - n)
    return [x0, y0, min(W, x + w + n) - x0, min(H, y + h + n) - y0]


def blood(box, ru, zh, en_lines, size, **kw):
    kw.setdefault("remove", "polish")
    kw["scale"] = kw.get("scale", 1.0) * 1.15
    return B(pad(box, 5, size), ru, zh, style="blood", en_lines=en_lines, maxw=kw.pop("maxw", box[2] + 6), **kw)


L[T + "c2_sicktavla1.tga"] = {"blocks": [blood([37, 29, 203, 138], ["ЧТО", "СКАЖЕШЬ?"], ["你觉得", "怎么样？"], 3,
                                               (256, 192), scale=1.2)]}
L[T + "c2_sicktavla2.tga"] = {"blocks": [blood([27, 32, 207, 108], ["ТЕБЕ", "НРАВИТСЯ?"], ["你喜欢", "这样吗？"], 2,
                                               (256, 192))]}
L[T + "c2_sicktavla3.tga"] = {"blocks": [blood([29, 33, 195, 138], ["ТЫ НИЧЕГО", "НЕ ЗНАЕШЬ"], ["你什么", "都不知道"], 3,
                                               (256, 192), scale=1.2)]}
L[T + "c2_sicktavla4.tga"] = {"blocks": [blood([23, 29, 206, 138], ["ЕСЛИ БЫ ТЫ", "ТОЛЬКО МОГ", "ПОНЯТЬ"],
                                               ["要是你", "能明白", "就好了"], 3, (256, 192))]}
L[T + "c2_sicktavla5.tga"] = {"blocks": [blood([18, 31, 220, 137], ["Я НИКОГДА", "ЭТОГО", "НЕ ХОТЕЛ"],
                                               ["我从来", "都不想", "这样"], 3, (256, 192))]}
L[T + "c2_sicktavla6.tga"] = {"blocks": [blood([21, 27, 220, 147], ["Я ХОЧУ, ЧТОБЫ", "ТЫ СДОХ...", "ПРОСТО... СДОХНИ"],
                                               ["我只想", "让你去死……", "去死吧"], 3, (256, 192))]}
L[T + "c2_psykwall2.tga"] = {"blocks": [blood([16, 18, 231, 138], "ПРОСНИСЬ", "醒醒", 1, (256, 256), scale=0.7)]}
L[T + "c2_psykwall3.tga"] = {"blocks": [blood([10, 128, 240, 110], ["ВОЗЬМИ", "КАМЕРУ"], ["拿起", "相机"], 2, (256, 256))]}
L[T + "c2_nightmares1.tga"] = {"blocks": [blood([53, 93, 151, 78], ["Я больше", "не могу"], ["我再也", "受不了了"], 2,
                                                (256, 256), drips=(0.15, 0.4))]}
L[T + "c2_nightmares2.tga"] = {"blocks": [blood([60, 95, 150, 60], "Я тебя ненавижу!", "我恨你!", 1, (256, 256),
                                                maxw=200, drips=(0.15, 0.6), remove="inpaint_ink", thr=22)]}
L[T + "c_nightmaremes2.tga"] = {"blocks": [blood([32, 73, 138, 48], "Я тебя ненавижу!", "我恨你!", 1, (192, 208),
                                                 maxw=170, drips=(0.15, 0.6), remove="inpaint_ink", thr=22)]}
L[T + "c2_helpme1.tga"] = {"blocks": [blood([68, 105, 171, 63], "Помоги мне", "救救我", 1, (256, 256), scale=0.75)]}
L[T + "c2_sktavla1.tga"] = {"blocks": [blood([37, 28, 184, 143], ["Берегись", "ДЕРЕВЬЕВ"], ["小心", "那些树"], 3,
                                             (256, 256), scale=1.2)]}
L[T + "r_note22.tga"] = {"blocks": [blood([5, 27, 152, 202], ["Код из", "десяти", "цифр...", "Стоит ли", "оно того?"],
                                          ["十个数字", "的密码……", "值得", "吗？"], 6, (160, 256), maxw=148,
                                          drips=(0.08, 0.3))]}
L[T + "r_doorblo1.tga"] = {"blocks": [blood([5, 22, 56, 30], "СДОХНИ", "去死", 1, (64, 128), maxw=56),
                                      blood([3, 75, 61, 27], "УХОДИ", "滚开", 1, (64, 128), maxw=58)]}
L[T + "r_sickif.tga"] = {"blocks": [B([0, 10, 256, 236], "ЕСЛИ?", "如果?", style="sketch", remove="polish",
                                      color=[235, 235, 235], size=100, rotate=12, center=[128, 132], maxw=225,
                                      hollow=2, rough=0.2)]}

# ---------------------------------------------------------------------------------------------- world: signs
def sign(box, ru, zh, style, size_wh, **kw):
    kw.setdefault("remove", "polish")
    return B(pad(box, kw.pop("pad", 2), size_wh), ru, zh, style=style, maxw=kw.pop("maxw", box[2] + 2), **kw)


L[T + "c2_stairsign.tga"] = {"blocks": [sign([5, 12, 118, 39], "ЛЕСТНИЦА", "楼梯", "plain", (128, 64),
                                             font={"russian": "condensed", "schinese": "cjk_black"},
                                             color=[22, 22, 22], outline=(1, [210, 205, 195]), maxw=120)]}
TB = dict(font={"russian": "sans", "schinese": "cjk_bold"}, color=[40, 40, 40], size=6, scale_cjk=1.3)
for name, ru, zh in (("c2_tbsign5.tga", "Поезда на север", "往北方向"), ("c2_tbsign6.tga", "Поезда на север", "往北方向")):
    L[T + name] = {"blocks": [sign([33, 4, 63, 10], ru, zh, "plain", (128, 16), maxw=84, center=[64.5, 8.5], **TB)]}
for name, ru, zh, w in (("c2_tbsignn1.tga", "Поезда до Форум-Некка", "开往 Forum Necka", 99),
                        ("c2_tbsignn1h.tga", "Поезда до Хансон-сквер", "开往汉森广场", 99),
                        ("c2_tbsignn2.tga", "Поезда до Мёрби-Сентр", "开往默比中心", 99),
                        ("c2_tbsignn3.tga", "Поезда до Норкасла", "开往诺卡斯尔", 92),
                        ("c2_tbsignn4.tga", "Поезда до Вест-Форест", "开往西森林", 99)):
    L[T + name] = {"blocks": [sign([9, 3, w, 10], ru, zh, "plain", (128, 16), maxw=w - 4, center=[9 + w / 2 - 2, 8.5],
                                   **TB)]}
for name, ru, zh, w in (("c2_tbsign2nn1.tga", "Тоннель 1", "隧道 1", 40), ("c2_tbsign2nn3.tga", "Тоннель 3", "隧道 3", 40),
                        ("c2_tbsign2nn5.tga", "Тоннель 1", "隧道 1", 40),
                        ("c2_tbsign2nn6.tga", "Тоннель 2,3", "隧道 2,3", 46)):
    L[T + name] = {"blocks": [sign([1, 3, w, 11], ru, zh, "plain", (128, 16), maxw=w + 4, align="left", left=2,
                                   center=[0, 8.5], remove="inpaint_ink", thr=30, **TB)]}
L[T + "c2_tbaninf.tga"] = {"blocks": [sign([92, 9, 68, 14], "Информация", "问讯处", "plain", (256, 32), maxw=96,
                                           align="left", left=92, font={"russian": "sans", "schinese": "cjk"},
                                           color=[235, 235, 235])]}
L[T + "c2_busskurt.tga"] = {"blocks": [sign([19, 7, 219, 25], "Расписание/Карта", "时刻表/地图", "plain", (256, 192),
                                            color=[20, 20, 20], maxw=230)]}
L[T + "c2_escsigns1.tga"] = {"blocks": [
    sign([52, 16, 41, 14], "ВВЕРХ", "上行", "plain", (256, 256), color=[40, 200, 40], maxw=70, center=[72.5, 23]),
    sign([54, 68, 37, 13], "ВНИЗ", "下行", "plain", (256, 256), color=[40, 200, 40], maxw=70, center=[72.5, 74.5]),
    sign([37, 129, 79, 12], "ВХОДА НЕТ", "禁止进入", "plain", (256, 256), color=[210, 30, 30], maxw=66,
         center=[77, 135]),
    sign([38, 187, 80, 13], "ВХОДА НЕТ", "禁止进入", "plain", (256, 256), color=[210, 30, 30], maxw=66,
         center=[78, 193.5])]}
PLATE = dict(font={"russian": "sans", "schinese": "cjk_bold"}, color=[30, 30, 28], loud=255, scale_cjk=1.4)
L[T + "c_hogskylt1.tga"] = {"blocks": [sign([21, 4, 50, 12], "психолог", "心理医生", "plain", (96, 48), maxw=70,
                                            center=[47, 10], size=7, **PLATE)]}
L[T + "c2_hogskylt1.tga"] = {"blocks": [sign([19, 3, 56, 12], "психолог", "心理医生", "plain", (96, 48), maxw=70,
                                             center=[47, 10], size=7, remove="inpaint_ink", thr=30, **PLATE)]}
L[T + "c_hogskylt2.tga"] = {"blocks": [sign([15, 3, 64, 13], "машинный зал", "机房", "plain", (96, 48),
                                            maxw=76, center=[47, 9.5], size=7, **PLATE)]}
L[T + "c2_hogskylt2.tga"] = {"blocks": [sign([13, 2, 68, 12], "машинный зал", "机房", "plain", (96, 48),
                                             maxw=76, center=[47, 9.5], size=7, remove="inpaint_ink", thr=30, **PLATE)]}
L[T + "c2_osign1.tga"] = {"blocks": [sign([11, 46, 233, 32], "РЕГИСТРАТУРА", "接待处", "plain", (256, 128),
                                          font={"russian": "serif", "schinese": "cjk_serif"}, color=[38, 36, 34],
                                          maxw=240)]}
L[T + "c3_asy_sign1.tga"] = {"blocks": [sign([6, 7, 88, 18], "РЕГИСТРАТУРА", "接待处", "plain", (96, 32),
                                             font={"russian": "condensed", "schinese": "cjk_bold"},
                                             color=[240, 240, 235], outline=(1, [30, 50, 30]), maxw=82, size=12,
                                             scale_cjk=0.85, center=[49, 16])]}
RUST = dict(font={"russian": "condensed", "schinese": "cjk_bold"}, color=[228, 225, 218],
            shadow=(1, 1, 0.6, [45, 32, 25], 0.7), scale_cjk=1.15)
for name, ru, zh, box in (("c3_asy_sign2.tga", ["РАЗДЕ-", "ВАЛКИ"], "更衣室", [5, 1, 86, 28]),
                          ("c3_asy_sign3.tga", ["ЖИЛЬЁ", "ПЕРСОНАЛА"], "员工宿舍", [14, 1, 67, 31]),
                          ("c3_asy_sign4.tga", "БОУЛИНГ", "保龄球馆", [7, 4, 83, 24]),
                          ("c3_asy_sign5.tga", ["БАСКЕТБОЛЬНАЯ", "ПЛОЩАДКА"], "篮球场", [11, 1, 73, 27])):
    L[T + name] = {"blocks": [sign(box, ru, zh, "plain", (96, 32), maxw=84, center=[48, 15.5],
                                   size=(10 if isinstance(ru, list) else 13), **RUST)]}
L[T + "c_donotenter.tga"] = {"blocks": [
    sign([18, 9, 27, 8], "СТОП", "停", "plain", (64, 64), font={"russian": "sans_bold", "schinese": "cjk_black"},
         color=[20, 20, 20], maxw=40, scale_cjk=1.3),
    sign([6, 31, 52, 18], ["НЕ", "ВХОДИТЬ"], ["禁止", "入内"], "plain", (64, 64),
         font={"russian": "sans_bold", "schinese": "cjk_black"}, color=[20, 20, 20], maxw=54, size=6,
         center=[32, 40], scale_cjk=1.25)]}
TAPE = dict(font={"russian": "black", "schinese": "cjk_black"}, color=[15, 15, 10], size=7, center=[0, 7.5],
            scale_cjk=1.2)
for name in ("c_polistejp.tga", "c_polistejpsick.tga", "c2_polistejpsic.tga"):
    L[T + name] = {"blocks": [sign([3, 3, 222, 9], "ПОЛИЦИЯ — НЕ ПЕРЕСЕКАТЬ", "警戒线 请勿越过", "plain", (256, 16),
                                   maxw=230, align="left", left=4, **TAPE)]}
L[T + "exit1.tga"] = {"blocks": [sign([4, 2, 57, 26], "ВЫХОД", "出口", "plain", (64, 32),
                                      font={"russian": "condensed", "schinese": "cjk_black"}, color=[255, 245, 200],
                                      glow=(2, [255, 220, 120], 0.6), maxw=58, scale_cjk=0.8)]}
L[T + "r_exit1.tga"] = {"blocks": [sign([0, 14, 64, 40], "ВЫХОД", "出口", "plain", (64, 64), remove="inpaint_ink",
                                        thr=25, font={"russian": "condensed", "schinese": "cjk_black"},
                                        color=[40, 235, 40], glow=(2, [40, 235, 40], 0.5), maxw=62, size=20,
                                        center=[32, 33])]}
L[T + "sign30.tga"] = {"blocks": [sign([9, 59, 46, 8], "ТУАЛЕТ", "洗手间", "plain", (64, 80),
                                       font={"russian": "sans_bold", "schinese": "cjk_bold"}, color=[240, 240, 240],
                                       maxw=48)]}

# ---------------------------------------------------------------------------------------------- bump maps
# The signs' relief: the English letters smoothed out where the sign is translated (the colour map carries the text).
G_ = "overlay/gfx/bumpmaps/"
for name in ("c2_tbsign5.tga", "c2_tbsign6.tga"):
    L[G_ + name] = {"blocks": [dict(box=[56, 3, 146, 26], remove="inpaint", radius=5, russian=None, schinese=None)]}
for name in ("c2_polistejp.tga", "c2_polistejpsic.tga", "c_polistejp.tga"):
    L[G_ + name] = {"blocks": [dict(box=[2, 2, 230, 12], remove="inpaint", radius=4, russian=None, schinese=None)]}
L[G_ + "c_donotenter.tga"] = {"blocks": [dict(box=[16, 8, 32, 11], remove=["color", [128, 128, 255], 1.0],
                                              russian=None, schinese=None),
                                         dict(box=[6, 29, 52, 20], remove=["color", [128, 128, 255], 1.0],
                                              russian=None, schinese=None)]}

# ---------------------------------------------------------------------------------------------- model textures
P = "models/Props/"
NEWS = dict(style="plain", font={"russian": "sans_bold", "schinese": "cjk_bold"}, color=[60, 68, 52])
L[P + "Blandat/e1tidning/tidning2.bmp"] = {"blocks": [
    B([86, 142, 244, 34], "Доктор найден мёртвым", "医生被发现死亡", remove="inpaint_ink", thr=40, kernel=31, size=22, center=[205, 159.5],
      maxw=236, **NEWS),
    B([86, 176, 178, 44], "Загадочно убит в своей квартире", "在自己的公寓中离奇遇害", remove="inpaint_ink", thr=40, kernel=31,
      size=13,
      align="left", left=89, first_center=185.5, maxw=172, wrap=True, pitch=19, **NEWS)]}
L[P + "Blandat/e2tidning/tidning2.bmp"] = {"blocks": [
    B([86, 136, 236, 94], "Подросток сбит машиной", "少年遭车撞", remove="inpaint_ink", thr=40, kernel=31, size=22,
      center=[200, 152],
      maxw=228, **NEWS),
    B([86, 136, 236, 94], "Полиция ищет свидетелей наезда, водитель скрылся.", "警方呼吁肇事逃逸事故的目击者提供线索。",
      remove="none", size=13, align="left", left=89, first_center=176, maxw=228, wrap=True, pitch=19, **NEWS)]}
L[P + "Blandat/dropbox/screen_on_fullbright.bmp"] = {"blocks": [
    B([30, 62, 196, 52], ["БРОСЬ", "МЕНЯ"], ["扔下", "我"], style="plain",
      font={"russian": "sans_bold", "schinese": "cjk_bold"}, color=[95, 95, 95], remove=["patch", 0, 132], size=46,
      center=[128, 128], maxw=210, feather=4),
    dict(box=[70, 140, 116, 52], remove=["patch", 0, 52], feather=4, russian=None, schinese=None)]}
for name, mirror in (("tex1.bmp", False), ("tex2.bmp", False), ("tex4.bmp", True), ("tex5.bmp", True)):
    L[P + "InomhusD/soptidning1/" + name] = {"blocks": [
        B([136, 164, 240, 94], ["Сдавайте сюда", "журналы"], ["旧杂志", "请投放于此"], style="plain",
          font={"russian": "sans_bold", "schinese": "cjk_bold"}, color=[200, 201, 202], remove="inpaint_ink",
          thr=30, size=16, center=[256, 216], maxw=230, mirror=mirror, scale_cjk=1.3)]}
L[P + "UtomhusD/sandlada/sand1.bmp"] = {"blocks": [
    B([70, 295, 201, 47], "ПЕСОК", "沙子", style="plain", font={"russian": "sans_bold", "schinese": "cjk_bold"},
      color=[97, 102, 93], remove="polish", size=33, center=[170.5, 318.5], maxw=190, mirror=True)]}
L["models/cutscene/chainsawguy3/tex1.bmp"] = phone(MOM, MESS1)

if __name__ == "__main__":
    import json
    with open(Path(__file__).with_name("labels.json"), "w", encoding="utf-8", newline="\n") as f:
        json.dump(L, f, ensure_ascii=False, indent=1)
        f.write("\n")
    print(len(L), "images")
