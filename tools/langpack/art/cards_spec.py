"""cards.json, the item and weapon cards make_art.py repaints: for each card (its path under overlay/), the English
lines as the card draws them (make_art finds them in the image by these), and the Russian and Chinese. The first
line is the title; each later element starts a new line and wraps when it is too wide.

    python cards_spec.py            (writes cards.json beside it)
"""
from pathlib import Path

I = "gfx/vgui/items/"
W = "gfx/vgui/weapons/"
PLATE_RU_L = ["Каменная табличка (левая половина)", "Похоже на левую половину каменной таблички.", "Гравировка частично отбита."]
PLATE_RU_R = ["Каменная табличка (правая половина)", "Похоже на правую половину каменной таблички.", "Гравировка частично отбита."]
PLATE_ZH_L = ["石板（左半块）", "看起来是一块石板的左半边。", "上面刻着什么，但已经断掉了。"]
PLATE_ZH_R = ["石板（右半块）", "看起来是一块石板的右半边。", "上面刻着什么，但已经断掉了。"]
PLATE_EN_L = ["Stone Plate (Left Half)", "Looks to be the left half of a stone plate.", "Something is engraved, but broken away."]
PLATE_EN_R = ["Stone Plate (Right Half)", "Looks to be the right half of a stone plate.", "Something is engraved, but broken away."]


def plate(nums):
    return (["Stone Plate", "Pieced together from two halves.", f"{nums} is engraved."],
            ["Каменная табличка", "Собрана из двух половин.", f"Выгравировано: {nums}"],
            ["石板", "由两半拼合而成。", f"上面刻着：{nums}"])


GLOCK_EN = ["Glock 19 9mm Pistol", "A common-spread handgun, renowned", "for reliability and stability. Pretty weak,",
            "but holds 15 rounds."]
GLOCK_RU = ["Пистолет Glock 19 9mm", "Популярный пистолет, известный надёжностью и стабильностью. Довольно слабый, "
            "но вмещает 15 патронов."]
GLOCK_ZH = ["格洛克 19 9mm 手枪", "一把常见的手枪，以可靠和稳定著称。威力较弱，但能装 15 发子弹。"]

CARDS = {
    I + "apartmentkey/640_apartmentkey_check.tga": (
        ["Apartment key", "A generic key for an apartment.", "There is a note next to it: “Neighbour across”"],
        ["Ключ от квартиры", "Обычный ключ от квартиры.", "Рядом с ним записка: «Сосед напротив»"],
        ["公寓钥匙", "一把普通的公寓钥匙。", "旁边有张纸条：“对门邻居”"]),
    I + "backdoorkey/640_backdoorkey_check.tga": (
        ["Back door key", "The tag says: Back door"],
        ["Ключ от задней двери", "На бирке написано: «Задняя дверь»"],
        ["后门钥匙", "标签上写着：后门"]),
    I + "basementkey/640_basement_check.tga": (
        ["Basement key", "The tag says: Basement"],
        ["Ключ от подвала", "На бирке написано: «Подвал»"],
        ["地下室钥匙", "标签上写着：地下室"]),
    I + "basementkey/640_basementkey_check.tga": (
        ["Basement key", "The tag says: Basement"],
        ["Ключ от подвала", "На бирке написано: «Подвал»"],
        ["地下室钥匙", "标签上写着：地下室"]),
    I + "card/640_card_check.tga": (
        ["Card", "A generic keycard with the name TL Trading."],
        ["Ключ-карта", "Обычная ключ-карта с надписью TL Trading."],
        ["门禁卡", "一张普通的门禁卡，上面印着 TL Trading 的名字。"]),
    I + "citysewerkey/640_citysewerkey_check.tga": (
        ["Ronald Street key", "A key with the tag; Ronald Street."],
        ["Ключ с Рональд-стрит", "Ключ с биркой: «Рональд-стрит»."],
        ["罗纳德街钥匙", "一把带标签的钥匙：罗纳德街。"]),
    I + "classkey/640_classkey_check.tga": (
        ["Classroom key", "It says “Classroom” on the tag."],
        ["Ключ от класса", "На бирке написано «Класс»."],
        ["教室钥匙", "标签上写着“教室”。"]),
    I + "doorknob/640_doorknob_check.tga": (
        ["Doorknob", "I can use this to open the door."],
        ["Дверная ручка", "С её помощью я смогу открыть дверь."],
        ["门把手", "我可以用它把门打开。"]),
    I + "entrancekey/640_entrancekey_check.tga": (
        ["Apartment key", "A key for an apartment. Found it in the hole."],
        ["Ключ от квартиры", "Ключ от квартиры. Нашёл его в дыре."],
        ["公寓钥匙", "一把公寓钥匙。在洞里找到的。"]),
    I + "firekey/640_firekey_check.TGA": (
        ["Fire exit key", "A key for fire exit doors."],
        ["Ключ от пожарного выхода", "Ключ от дверей пожарного выхода."],
        ["消防出口钥匙", "开消防出口门的钥匙。"]),
    I + "foot/640_foot_check.tga": (
        ["Rotten foot", "I must be crazy for carrying this around..."],
        ["Гнилая ступня", "Я, наверное, сошёл с ума, раз таскаю это с собой..."],
        ["腐烂的脚", "带着这东西到处走，我一定是疯了……"]),
    I + "fsewerkey/640_fsewerkey_check.TGA": (
        ["Sewer key", "A generic key, found in the sewer."],
        ["Ключ от канализации", "Обычный ключ, найденный в канализации."],
        ["下水道钥匙", "一把普通的钥匙，在下水道里找到的。"]),
    I + "fuse/640_fuse_check.tga": (
        ["Fuse", "It allows wires to give electricity", "to certain things."],
        ["Предохранитель", "Через него провода подают ток к нужным вещам."],
        ["保险丝", "能让电线给某些东西供电。"]),
    I + "gatekey/640_gatekey_check.tga": (
        ["Gate key", "The tag says: Park gate"],
        ["Ключ от ворот", "На бирке написано: «Ворота парка»"],
        ["大门钥匙", "标签上写着：公园大门"]),
    I + "gatekey2/640_gatekey2_check.TGA": (
        ["Gate key", "The key I got from the doctor."],
        ["Ключ от ворот", "Ключ, который мне дал доктор."],
        ["大门钥匙", "医生给我的钥匙。"]),
    I + "hallkey/640_hallkey_check.tga": (
        ["Hall key", "It looks like a kind of key for hall doors."],
        ["Ключ от холла", "Похоже, это ключ от дверей холла."],
        ["大厅钥匙", "看起来像是开大厅门的钥匙。"]),
    I + "ladder/640_ladder_check.tga": (
        ["Ladder", "A small ladder. High enough to reach", "a window."],
        ["Лестница", "Небольшая лестница. Достаточно высокая, чтобы достать до окна."],
        ["梯子", "一架小梯子。高度足够够到窗户。"]),
    I + "lighter/640_lighter_check.tga": (
        ["Lighter", "A lighter. I can use it to set the oil on fire."],
        ["Зажигалка", "Зажигалка. Ею можно поджечь масло."],
        ["打火机", "一个打火机。我可以用它点燃油。"]),
    I + "package/640_package_check.TGA": (
        ["Weird package", "I don't know what I should do with this."],
        ["Странная посылка", "Не знаю, что мне с этим делать."],
        ["奇怪的包裹", "我不知道该拿它怎么办。"]),
    I + "phone_battery/640_pbattery_check.tga": (
        ["Phone Battery", "A battery for my phone. Hope it's charged."],
        ["Аккумулятор для телефона", "Аккумулятор для моего телефона. Надеюсь, он заряжен."],
        ["手机电池", "我手机的电池。希望是充满电的。"]),
    I + "plate/640_plate1_check.tga": plate("V - I - I - VII - III - VIII - IX - VI"),
    I + "plate/640_plate2_check.tga": plate("V - I - I - VII - III - IX - VII - IX"),
    I + "plate/640_plate3_check.tga": plate("V - I - I - VII - VII - VIII - I - VI"),
    I + "plate/640_plate1_left_check.tga": (PLATE_EN_L, PLATE_RU_L, PLATE_ZH_L),
    I + "plate/640_plate2_left_check.tga": (PLATE_EN_L, PLATE_RU_L, PLATE_ZH_L),
    I + "plate/640_plate3_left_check.tga": (PLATE_EN_L, PLATE_RU_L, PLATE_ZH_L),
    I + "plate/640_plate1_right_check.tga": (PLATE_EN_R, PLATE_RU_R, PLATE_ZH_R),
    I + "plate/640_plate2_right_check.tga": (PLATE_EN_R, PLATE_RU_R, PLATE_ZH_R),
    I + "plate/640_plate3_right_check.tga": (PLATE_EN_R, PLATE_RU_R, PLATE_ZH_R),
    I + "rope/640_rope_check.tga": (
        ["Rope", "Enough rope to climb down the balcony."],
        ["Верёвка", "Достаточно длинная, чтобы спуститься с балкона."],
        ["绳子", "长度足够从阳台爬下去。"]),
    I + "rustykey/640_rustykey_check.tga": (
        ["Rusty key", "A rusty key. Found it in his stomach."],
        ["Ржавый ключ", "Ржавый ключ. Нашёл его у него в желудке."],
        ["生锈的钥匙", "一把生锈的钥匙。在他的胃里找到的。"]),
    I + "saw/640_saw_check.tga": (
        ["Saw", "A saw. Should be able to cut through flesh."],
        ["Пила", "Пила. Ею можно резать плоть."],
        ["锯子", "一把锯子。应该能锯开血肉。"]),
    I + "scissors/640_scissors_check.tga": (
        ["Scissors", "A pair of scissors. Should work to cut", "the rope with."],
        ["Ножницы", "Ножницы. Ими можно перерезать верёвку."],
        ["剪刀", "一把剪刀。应该能用来剪断绳子。"]),
    I + "scissors/640_scissorsleft_check.TGA": (
        ["Scissors blade (left half)", "Not enough to cut the rope with.", "I could use another part..."],
        ["Лезвие ножниц (левая половина)", "Этим верёвку не перерезать.", "Нужна вторая часть..."],
        ["剪刀刃（左半边）", "单靠这个剪不断绳子。", "我还需要另一半……"]),
    I + "scissors/640_scissorsright_check.TGA": (
        ["Scissors blade (right half)", "Not enough to cut the rope with.", "I could use another part..."],
        ["Лезвие ножниц (правая половина)", "Этим верёвку не перерезать.", "Нужна вторая часть..."],
        ["剪刀刃（右半边）", "单靠这个剪不断绳子。", "我还需要另一半……"]),
    I + "screwdriver/640_screwdriver_check.TGA": (
        ["Screwdriver", "An ordinary screwdriver.", "Too rusty to be used as a weapon."],
        ["Отвёртка", "Обычная отвёртка.", "Слишком ржавая, чтобы служить оружием."],
        ["螺丝刀", "一把普通的螺丝刀。", "锈得太厉害，没法当武器用。"]),
    I + "securitykey/640_securitykey_check.tga": (
        ["Security Key.", "I should be able to find my phone with this."],
        ["Ключ охраны", "С его помощью я смогу найти свой телефон."],
        ["保安钥匙", "有了它，我应该能找到我的手机。"]),
    I + "sewerkey/640_sewerkey_check.tga": (
        ["Sewer key", "I wonder where I can use this?", "Probably somewhere nearby."],
        ["Ключ от канализации", "Интересно, где его можно использовать?", "Наверное, где-то рядом."],
        ["下水道钥匙", "不知道能在哪儿用？", "大概就在附近。"]),
    I + "spade/640_spade_check.tga": (
        ["Spade", "I can dig holes in certain patches with this.", "Too flimsy to be used as a weapon."],
        ["Лопата", "Ею можно копать ямы в определённых местах.", "Слишком хлипкая, чтобы служить оружием."],
        ["铁锹", "我可以用它在某些地方挖坑。", "太不结实，没法当武器用。"]),
    I + "staffkey/640_staffkey_check.tga": (
        ["Staff Key", "There is a message on the key saying", "“Staff Key”"],
        ["Ключ для персонала", "На ключе надпись: «Ключ для персонала»"],
        ["员工钥匙", "钥匙上写着“员工钥匙”"]),
    I + "taclight/640_taclight_check.tga": (
        ["Glock Tactical Light", "A small, lightweight flashlight that can be", "attached to a Glock handgun. The extra",
         "weight on the barrel affects accuracy."],
        ["Тактический фонарь для Glock", "Небольшой лёгкий фонарь, который крепится на пистолет Glock. Лишний вес "
         "на стволе снижает точность."],
        ["格洛克战术灯", "一个小巧轻便的手电筒，可以装在格洛克手枪上。枪管上多出的重量会影响精准度。"]),
    I + "tapes/640_tape_check.tga": (
        ["Cassette Tape", "I should be able to save my progress", "with this. Use is limited."],
        ["Аудиокассета", "С её помощью я смогу сохранить прогресс.", "Число использований ограничено."],
        ["磁带", "我可以用它保存进度。", "使用次数有限。"]),
    I + "valve/640_valve_check.tga": (
        ["Valve", "I can place this valve somewhere and use it."],
        ["Вентиль", "Этот вентиль можно где-то установить и использовать."],
        ["阀门", "我可以把这个阀门装到某个地方使用。"]),
    I + "videotape/640_videotape_check.tga": (
        ["Video tape", "A blood-stained video tape.", "I need a video player to view it."],
        ["Видеокассета", "Окровавленная видеокассета.", "Чтобы её посмотреть, нужен видеоплеер."],
        ["录像带", "一盘沾满血迹的录像带。", "我需要录像机才能观看。"]),

    W + "axe/640_axe_check.TGA": (
        ["Fireaxe", "A powerful fireaxe.", "I can deal a lot of damage with this."],
        ["Пожарный топор", "Мощный пожарный топор.", "Им можно нанести много урона."],
        ["消防斧", "一把威力强大的消防斧。", "用它能造成大量伤害。"]),
    W + "booklaser/640_booklaser_check.TGA": (
        ["Simon's Book", "A bloody book. I can feel a strange", "power emanating from it."],
        ["Книга Саймона", "Окровавленная книга. Я чувствую, как от неё исходит странная сила."],
        ["西蒙的书", "一本沾血的书。我能感到一股奇怪的力量从中散发出来。"]),
    W + "branch/640_branch_check.TGA": (
        ["Branch", "A tree branch. Not really a weapon", "but at least something useful out here."],
        ["Ветка", "Ветка дерева. Не совсем оружие, но здесь хоть что-то полезное."],
        ["树枝", "一根树枝。算不上武器，", "但在这里总算有点用处。"]),
    W + "camera/640_camera_check.TGA": (
        ["Camera of Death", "Freezes all enemies."],
        ["Камера смерти", "Замораживает всех врагов."],
        ["死亡相机", "冻结所有敌人。"]),
    W + "famas/640_famas_check.TGA": (
        ["FAMAS G2", "A powerful assault rifle", "Carries infinite ammo."],
        ["FAMAS G2", "Мощная штурмовая винтовка.", "Бесконечные патроны."],
        ["FAMAS G2", "一把威力强大的突击步枪。", "弹药无限。"]),
    W + "flare/640_flare_check.tga": (
        ["Flare", "A slow-burning road flare, used in", "emergencies. Emits a bright red light and",
         "burns for about 60 seconds"],
        ["Сигнальный факел", "Медленно горящий дорожный факел для экстренных случаев. Даёт яркий красный свет "
         "и горит около 60 секунд."],
        ["信号棒", "一根缓慢燃烧的道路信号棒，用于紧急情况。发出明亮的红光，可燃烧约 60 秒。"]),
    W + "flashlight/640_flashlight_check.TGA": (
        ["Flashlight", "A standard battery-powered flashlight.", "Casts a medium-sized beam."],
        ["Фонарик", "Обычный фонарик на батарейках.", "Даёт луч средней ширины."],
        ["手电筒", "一支普通的电池手电筒。", "光束大小适中。"]),
    W + "glock/640_glock_check.tga": (GLOCK_EN, GLOCK_RU, GLOCK_ZH),
    W + "glocktac/640_glock_check.tga": (
        GLOCK_EN[:3] + ["but holds 15 rounds. Tactical light attached."],
        GLOCK_RU[:1] + [GLOCK_RU[1] + " С тактическим фонарём."],
        GLOCK_ZH[:1] + [GLOCK_ZH[1] + "已装上战术灯。"]),
    W + "lantern/640_lantern_check.tga": (
        ["A dim lantern.", "I should be able to light my way with this"],
        ["Тусклый фонарь.", "С ним я смогу освещать себе путь"],
        ["一盏昏暗的提灯。", "我应该能用它照亮前路"]),
    W + "m16/640_m16_check.tga": (
        ["Stag Arms AR-15", "A powerful assault rifle, capable of", "firing single shot or 3 round bursts.",
         "Uses a 20 round box magazine."],
        ["Stag Arms AR-15", "Мощная штурмовая винтовка, стреляет одиночными или очередями по 3 патрона. "
         "Магазин на 20 патронов."],
        ["Stag Arms AR-15", "一把威力强大的突击步枪，可单发或三连发射击。使用 20 发弹匣。"]),
    W + "mobile/640_mobile_check.tga": (
        ["Mobile Phone", "Simon's mobile phone. Phone screen", "can be accessed by selecting the “use”", "option."],
        ["Мобильный телефон", "Мобильный телефон Саймона. Экран телефона открывается пунктом «Применить»."],
        ["手机", "西蒙的手机。选择“使用”选项即可打开手机屏幕。"]),
    W + "mp5/640_mp5_check.tga": (
        ["MP5", "A light and fast submachine gun. Carries", "30 rounds. Ammo is limited, so use it", "carefully"],
        ["MP5", "Лёгкий скорострельный пистолет-пулемёт. Вмещает 30 патронов. Патронов мало, так что "
         "берегите их."],
        ["MP5", "一把轻便快速的冲锋枪。可装 30 发子弹。弹药有限，请谨慎使用。"]),
    W + "mp9/640_mp9_check.tga": (
        ["Brügger & Thomet MP9", "A light-weight machine pistol with an", "extremely high rate of fire. Hard to",
         "control and weak."],
        ["Brügger & Thomet MP9", "Лёгкий автоматический пистолет с очень высоким темпом стрельбы. Слабый "
         "и плохо управляемый."],
        ["Brügger & Thomet MP9", "一把轻型冲锋手枪，射速极高。难以控制，威力也弱。"]),
    W + "nightstick/640_nightstick_check.tga": (
        ["Nightstick", "A strong, synthetic nightstick that can", "deliver a powerful blow when used",
         "properly, although somewhat cubersome."],
        ["Дубинка", "Прочная синтетическая дубинка. При умелом обращении бьёт очень сильно, хоть и "
         "несколько громоздкая."],
        ["警棍", "一根结实的合成材料警棍，使用得当能造成有力的打击，只是有些笨重。"]),
    W + "p345/640_p345_check.TGA": (
        ["P345 Ruger .45 ACP Pistol", "A solid, high-powered handgun", "that packs a real punch. 8 round mag."],
        ["Пистолет Ruger P345 .45 ACP", "Надёжный мощный пистолет с серьёзной убойной силой.",
         "Магазин на 8 патронов."],
        ["鲁格 P345 .45 ACP 手枪", "一把扎实的大威力手枪，火力十足。", "弹匣容量 8 发。"]),
    W + "radio/640_radio_check.TGA": (
        ["Radio", "I can talk through this."],
        ["Рация", "Через неё можно переговариваться."],
        ["对讲机", "我可以用它通话。"]),
    W + "revolver/640_revolver_check.tga": (
        ["Taurus .357 magnum", "High power snub-nosed revolver with", "a heavy kick. Holds 5 rounds.."],
        ["Taurus .357 Magnum", "Мощный короткоствольный револьвер с сильной отдачей. Вмещает 5 патронов."],
        ["金牛座 .357 马格南", "大威力短管左轮手枪，后坐力很强。", "可装 5 发子弹。"]),
    W + "rifle/640_rifle_check.TGA": (
        ["Sniper rifle", "A high powered scoped rifle. Very long range.", "Extremely accurate when breath is held."],
        ["Снайперская винтовка", "Мощная винтовка с оптическим прицелом. Очень большая дальность.",
         "Если задержать дыхание, бьёт крайне точно."],
        ["狙击步枪", "一把装有瞄准镜的大威力步枪。射程极远。", "屏住呼吸时极其精准。"]),
    W + "shotgun/640_shotgun_check.tga": (
        ["Remington 870 Shotgun", "A world-renowned pump-action Shotgun.", "Deals a devastating amount of damage",
         "at close range."],
        ["Дробовик Remington 870", "Всемирно известный помповый дробовик.", "Наносит сокрушительный урон вблизи."],
        ["雷明顿 870 霰弹枪", "举世闻名的泵动式霰弹枪。", "近距离能造成毁灭性伤害。"]),
    W + "sledgehammer/640_sledgehammer_check.tga": (
        ["Sledgehammer", "Powerful, but heavy to use."],
        ["Кувалда", "Мощная, но тяжёлая."],
        ["大锤", "威力大，但用起来很沉。"]),
    W + "switchblade/640_switchblade_check.tga": (
        ["Switchblade", "Easy to handle, but rather weak", "with an ineffective range."],
        ["Выкидной нож", "Удобный, но довольно слабый,", "и бьёт совсем недалеко."],
        ["弹簧刀", "容易上手，但威力较弱，", "攻击范围也很小。"]),
    W + "syringe/640_syringe_check.TGA": (
        ["Morphine Syringe", "A syringe containing one dose of morphine.", "Restores health, but too much can be",
         "dangerous."],
        ["Шприц с морфином", "Шприц с одной дозой морфина.", "Восстанавливает здоровье, но передозировка опасна."],
        ["吗啡注射器", "装有一剂吗啡的注射器。", "可以恢复生命值，但用多了会有危险。"]),
    W + "vp70/640_vp70_check.TGA": (
        ["VP70", "A fairly powerful 3 round burst pistol"],
        ["VP70", "Довольно мощный пистолет, стреляющий очередями по 3 патрона"],
        ["VP70", "一把威力不错的三连发手枪"]),
}

if __name__ == "__main__":
    import json
    out = {rel: {"english": en, "russian": ru, "schinese": zh} for rel, (en, ru, zh) in CARDS.items()}
    with open(Path(__file__).with_name("cards.json"), "w", encoding="utf-8", newline="\n") as f:
        json.dump(out, f, ensure_ascii=False, indent=1)
        f.write("\n")
    print(len(out), "cards")
