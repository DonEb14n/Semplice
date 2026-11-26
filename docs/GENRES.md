# Genre vocabulary (draft for review)

Generated from `resources/tags/genres.json` by `scripts/genres_to_markdown.ps1` - edit the JSON, not this file.

**What to check**
- Is every genre you want here, and is nothing here that you do not want? (`key` is the permanent database key; names can be changed freely, keys should not.)
- The **ru / zh_CN / ja names are machine drafts**.
- **related** (+0.20 in the recommender) and **opposite** (-0.30) relations are symmetric; the file is refused at start-up if they are not.
- Aliases are the spellings found in real tags; every spelling must mean exactly one genre. All four names and the ID3v1 names also work as aliases.

Genres: 76

## Genres

| key | en | ru | zh_CN | ja | related | opposite |
|---|---|---|---|---|---|---|
| `rock` | Rock | Рок | 摇滚 | ロック | alternative-rock, blues, folk-rock, grunge, hard-rock, indie, progressive-rock, psychedelic-rock, punk |  |
| `hard-rock` | Hard Rock | Хард-рок | 硬摇滚 | ハードロック | grunge, heavy-metal, metal, rock |  |
| `progressive-rock` | Progressive Rock | Прогрессивный рок | 前卫摇滚 | プログレッシブ・ロック | jazz-fusion, post-rock, psychedelic-rock, rock |  |
| `psychedelic-rock` | Psychedelic Rock | Психоделический рок | 迷幻摇滚 | サイケデリック・ロック | folk-rock, progressive-rock, rock, shoegaze |  |
| `alternative-rock` | Alternative Rock | Альтернативный рок | 另类摇滚 | オルタナティブ・ロック | britpop, emo, grunge, indie, post-punk, rock, shoegaze |  |
| `indie` | Indie | Инди | 独立音乐 | インディー | alternative-rock, britpop, indie-pop, rock, shoegaze |  |
| `punk` | Punk | Панк | 朋克 | パンク | emo, hardcore, post-punk, rock, ska | classical, easy-listening, smooth-jazz |
| `post-punk` | Post-Punk | Пост-панк | 后朋克 | ポストパンク | alternative-rock, gothic, new-wave, punk |  |
| `grunge` | Grunge | Гранж | 油渍摇滚 | グランジ | alternative-rock, hard-rock, rock |  |
| `post-rock` | Post-Rock | Пост-рок | 后摇滚 | ポストロック | ambient, experimental, progressive-rock |  |
| `shoegaze` | Shoegaze | Шугейз | 鞋盯 | シューゲイザー | alternative-rock, ambient, indie, psychedelic-rock |  |
| `emo` | Emo | Эмо | 情绪硬核 | エモ | alternative-rock, hardcore, indie-pop, punk |  |
| `hardcore` | Hardcore | Хардкор | 硬核 | ハードコア | emo, punk, thrash-metal | ambient |
| `new-wave` | New Wave | Нью-вейв | 新浪潮 | ニュー・ウェイヴ | post-punk, synth-pop, synthwave |  |
| `gothic` | Gothic | Готик | 哥特 | ゴシック | black-metal, industrial, post-punk |  |
| `britpop` | Britpop | Брит-поп | 英伦摇滚 | ブリットポップ | alternative-rock, indie, indie-pop |  |
| `folk-rock` | Folk Rock | Фолк-рок | 民谣摇滚 | フォーク・ロック | country, folk, psychedelic-rock, rock |  |
| `metal` | Metal | Метал | 金属 | メタル | black-metal, death-metal, hard-rock, heavy-metal, thrash-metal | children, easy-listening, lo-fi, new-age, smooth-jazz |
| `heavy-metal` | Heavy Metal | Хэви-метал | 重金属 | ヘヴィメタル | hard-rock, metal, thrash-metal | children, easy-listening, new-age, smooth-jazz |
| `thrash-metal` | Thrash Metal | Трэш-метал | 鞭挞金属 | スラッシュメタル | death-metal, hardcore, heavy-metal, metal | ambient, children, easy-listening, new-age |
| `death-metal` | Death Metal | Дэт-метал | 死亡金属 | デスメタル | black-metal, metal, thrash-metal | children, classical, country, easy-listening, new-age |
| `black-metal` | Black Metal | Блэк-метал | 黑金属 | ブラックメタル | death-metal, gothic, metal | children, easy-listening, new-age, pop |
| `pop` | Pop | Поп | 流行音乐 | ポップ | dance, indie-pop, j-pop, k-pop, r-and-b, synth-pop | black-metal |
| `synth-pop` | Synth-Pop | Синти-поп | 合成器流行 | シンセポップ | electronic, new-wave, pop, synthwave |  |
| `indie-pop` | Indie Pop | Инди-поп | 独立流行 | インディーポップ | britpop, emo, indie, pop |  |
| `k-pop` | K-Pop | К-поп | 韩国流行音乐 | Kポップ | hip-hop, j-pop, pop |  |
| `j-pop` | J-Pop | Джей-поп | 日本流行音乐 | Jポップ | anime, k-pop, pop |  |
| `anime` | Anime | Аниме | 动漫音乐 | アニメ | j-pop, soundtrack |  |
| `dance` | Dance | Танцевальная | 舞曲 | ダンス | disco, electronic, house, pop |  |
| `electronic` | Electronic | Электронная | 电子音乐 | エレクトロニック | ambient, dance, downtempo, drum-and-bass, dubstep, house, industrial, synth-pop, synthwave, techno, trance, trip-hop |  |
| `house` | House | Хаус | 浩室音乐 | ハウス | dance, electronic, techno, trance |  |
| `techno` | Techno | Техно | 科技舞曲 | テクノ | electronic, house, industrial, trance | country, folk |
| `trance` | Trance | Транс | 出神音乐 | トランス | electronic, house, techno |  |
| `drum-and-bass` | Drum and Bass | Драм-н-бейс | 鼓打贝斯 | ドラムンベース | dubstep, electronic | folk |
| `dubstep` | Dubstep | Дабстеп | 回响贝斯 | ダブステップ | drum-and-bass, dub, electronic, trap | folk |
| `ambient` | Ambient | Эмбиент | 氛围音乐 | アンビエント | chillout, downtempo, electronic, lo-fi, new-age, post-rock, shoegaze | hardcore, thrash-metal |
| `downtempo` | Downtempo | Даунтемпо | 缓拍音乐 | ダウンテンポ | ambient, chillout, electronic, lo-fi, trip-hop |  |
| `trip-hop` | Trip Hop | Трип-хоп | 神游舞曲 | トリップホップ | downtempo, electronic, hip-hop |  |
| `synthwave` | Synthwave | Синтвейв | 合成器浪潮 | シンセウェイヴ | electronic, new-wave, synth-pop |  |
| `industrial` | Industrial | Индастриал | 工业音乐 | インダストリアル | electronic, gothic, techno |  |
| `chillout` | Chillout | Чилаут | 放松音乐 | チルアウト | ambient, downtempo |  |
| `lo-fi` | Lo-Fi | Лоу-фай | 低保真 | ローファイ | ambient, downtempo | metal |
| `hip-hop` | Hip-Hop | Хип-хоп | 嘻哈 | ヒップホップ | funk, k-pop, r-and-b, rap, trap, trip-hop |  |
| `rap` | Rap | Рэп | 说唱 | ラップ | hip-hop, trap |  |
| `trap` | Trap | Трэп | 陷阱音乐 | トラップ | dubstep, hip-hop, rap | classical, country, folk |
| `r-and-b` | R&B | Ритм-н-блюз | 节奏布鲁斯 | R&B | funk, hip-hop, pop, soul |  |
| `soul` | Soul | Соул | 灵魂乐 | ソウル | blues, disco, funk, gospel, jazz, r-and-b |  |
| `funk` | Funk | Фанк | 放克 | ファンク | afrobeat, disco, hip-hop, jazz-fusion, r-and-b, soul |  |
| `disco` | Disco | Диско | 迪斯科 | ディスコ | dance, funk, soul |  |
| `jazz` | Jazz | Джаз | 爵士乐 | ジャズ | blues, bossa-nova, jazz-fusion, smooth-jazz, soul |  |
| `smooth-jazz` | Smooth Jazz | Смус-джаз | 轻爵士 | スムースジャズ | easy-listening, jazz | heavy-metal, metal, punk |
| `jazz-fusion` | Jazz Fusion | Джаз-фьюжн | 融合爵士 | フュージョン | funk, jazz, progressive-rock |  |
| `bossa-nova` | Bossa Nova | Босса-нова | 波萨诺瓦 | ボサノヴァ | jazz, latin |  |
| `blues` | Blues | Блюз | 蓝调 | ブルース | gospel, jazz, rock, soul |  |
| `country` | Country | Кантри | 乡村音乐 | カントリー | folk, folk-rock, gospel | death-metal, techno, trap |
| `folk` | Folk | Фолк | 民谣 | フォーク | country, folk-rock, world | drum-and-bass, dubstep, techno, trap |
| `classical` | Classical | Классическая | 古典音乐 | クラシック | opera, soundtrack | death-metal, punk, trap |
| `opera` | Opera | Опера | 歌剧 | オペラ | classical |  |
| `soundtrack` | Soundtrack | Саундтрек | 原声音乐 | サウンドトラック | anime, classical |  |
| `reggae` | Reggae | Регги | 雷鬼 | レゲエ | dub, ska |  |
| `ska` | Ska | Ска | 斯卡 | スカ | punk, reggae |  |
| `dub` | Dub | Даб | 配音音乐 | ダブ | dubstep, reggae |  |
| `latin` | Latin | Латиноамериканская | 拉丁音乐 | ラテン | bossa-nova, salsa, world |  |
| `salsa` | Salsa | Сальса | 萨尔萨 | サルサ | latin |  |
| `world` | World | Музыка мира | 世界音乐 | ワールドミュージック | afrobeat, folk, latin |  |
| `afrobeat` | Afrobeat | Афробит | 非洲节拍 | アフロビート | funk, world |  |
| `gospel` | Gospel | Госпел | 福音音乐 | ゴスペル | blues, country, soul |  |
| `new-age` | New Age | Нью-эйдж | 新世纪音乐 | ニューエイジ | ambient, easy-listening | black-metal, death-metal, heavy-metal, metal, thrash-metal |
| `easy-listening` | Easy Listening | Лёгкая музыка | 轻音乐 | イージーリスニング | new-age, smooth-jazz | black-metal, death-metal, heavy-metal, metal, punk, thrash-metal |
| `experimental` | Experimental | Экспериментальная | 实验音乐 | エクスペリメンタル | noise, post-rock |  |
| `noise` | Noise | Нойз | 噪音音乐 | ノイズ | experimental |  |
| `instrumental` | Instrumental | Инструментальная | 器乐 | インストゥルメンタル |  |  |
| `a-cappella` | A Cappella | А капелла | 无伴奏合唱 | アカペラ |  |  |
| `spoken-word` | Spoken Word | Разговорный жанр | 口语 | スポークンワード |  |  |
| `comedy` | Comedy | Комедия | 喜剧 | コメディ |  |  |
| `children` | Children's | Детская | 儿童音乐 | 子供向け |  | black-metal, death-metal, heavy-metal, metal, thrash-metal |

## Aliases

| key | aliases |
|---|---|
| `rock` | Rock & Roll; Rock'n'Roll; Rock and Roll; Rock n Roll; Rock Music; Classic Rock; Roots Rock |
| `hard-rock` | Hard-Rock; Hardrock |
| `progressive-rock` | Prog Rock; Prog-Rock; Progressive; Prog |
| `psychedelic-rock` | Psychedelic; Psych Rock; Psych; Acid Rock |
| `alternative-rock` | Alternative; Alt Rock; Alt-Rock; Alternative & Punk; Alternative/Indie |
| `indie` | Indie Rock; Independent; Indie/Alternative |
| `punk` | Punk Rock; Pop Punk; Punk Music |
| `post-punk` | Post Punk; Postpunk |
| `grunge` | Seattle Sound |
| `post-rock` | Post Rock; Postrock |
| `shoegaze` | Shoegazing; Dream Pop |
| `emo` | Emocore; Emo Rock |
| `hardcore` | Hardcore Punk; Hard Core |
| `new-wave` | Newwave; New Romantic; NDW; Neue Deutsche Welle |
| `gothic` | Goth; Gothic Rock; Darkwave; Goth Rock |
| `britpop` | Brit Pop; Brit-Pop; BritPop |
| `folk-rock` | Folk/Rock; Folk-Rock |
| `metal` | Metal Music |
| `heavy-metal` | Heavy; Classic Metal; NWOBHM |
| `thrash-metal` | Thrash |
| `death-metal` | Death; Brutal Death Metal; Melodic Death Metal |
| `black-metal` | Black |
| `pop` | Pop Music; Pop/Rock; Pop Rock; Pop-Rock; Popular |
| `synth-pop` | Synthpop; Synth Pop; Electropop |
| `indie-pop` | Indiepop; Twee Pop |
| `k-pop` | KPop; Korean Pop |
| `j-pop` | JPop; Japanese Pop |
| `anime` | Anison; Anime Music |
| `dance` | Dance Music; Eurodance; Club; Dance Pop |
| `electronic` | Electronica; Electronic Music; Electro; EDM; Elektronik; Electronic/Dance; Electronic & Dance; Big Beat; Breakbeat; IDM; Electroclash |
| `house` | House Music; Deep House; Progressive House; Tech House; Euro-House; Club-House |
| `techno` | Techno Music; Detroit Techno; Euro-Techno; Rave |
| `trance` | Psytrance; Goa Trance; Progressive Trance; Goa |
| `drum-and-bass` | Drum & Bass; Drum n Bass; DnB; D&B; Drum'n'Bass; Jungle |
| `dubstep` | Dub Step; Brostep |
| `ambient` | Ambient Music; Dark Ambient; Illbient; Psybient |
| `downtempo` | Down Tempo; Downbeat |
| `trip-hop` | Trip-Hop; Triphop; Bristol Sound |
| `synthwave` | Synth Wave; Retrowave; Outrun |
| `industrial` | Industrial Rock; EBM; Industrial Metal; Techno-Industrial; Industro-Goth |
| `chillout` | Chill Out; Chill; Chillwave |
| `lo-fi` | Lofi; Lo Fi; Lo-Fi Hip Hop |
| `hip-hop` | Hip Hop; Hip-Hop/Rap; HipHop; Hiphop; Rap/Hip-Hop; Hip Hop/Rap; Gangsta; Gangsta Rap; G-Funk; Booty Bass |
| `rap` | Rap Music; Rapping; Christian Rap; Christian Gangsta Rap |
| `trap` | Trap Music; Trap Rap |
| `r-and-b` | RnB; R'n'B; R & B; Rhythm and Blues; Rhythm & Blues; R&B/Soul; Contemporary R&B; Slow Jam |
| `soul` | Soul Music; Neo Soul; Northern Soul; Motown; Rhythmic Soul |
| `funk` | Funk Music; P-Funk; Pop/Funk |
| `disco` | Disco Music; Nu-Disco; Italo Disco |
| `jazz` | Jazz Music; Bebop; Bebob; Swing; Big Band; Acid Jazz; Cool Jazz; Free Jazz; Vocal Jazz; Jazz/Blues |
| `smooth-jazz` | Contemporary Jazz |
| `jazz-fusion` | Fusion; Jazz-Funk; Jazz Funk; Fast Fusion |
| `bossa-nova` | Bossanova |
| `blues` | Blues Music; Delta Blues; Chicago Blues; Blues Rock |
| `country` | Country Music; Country & Western; Country/Western; Americana; Bluegrass; Alt-Country; Alt Country |
| `folk` | Folk Music; Singer/Songwriter; Singer-Songwriter; Acoustic; Folklore; Celtic; Traditional; Roots; Polka; Pop-Folk |
| `classical` | Classical Music; Baroque; Chamber Music; Symphony; Sonata; Neoclassical; Orchestral; Piano |
| `opera` | Opera Music; Operetta |
| `soundtrack` | Soundtracks; OST; Film Score; Movie Soundtrack; Score; Original Soundtrack; Musical; Showtunes; Trailer; Game Music; Video Game Music |
| `reggae` | Reggae Music; Roots Reggae; Dancehall; Dance Hall |
| `ska` | Ska Punk; Two Tone; 2 Tone |
| `dub` | Dub Music; Dub Reggae |
| `latin` | Latin Music; Latino; Latin Pop; Merengue; Tango; Samba; Reggaeton |
| `salsa` | Salsa Music; Mambo |
| `world` | World Music; Worldbeat; Ethnic; International; Global; Tribal; Native American; Bhangra |
| `afrobeat` | Afro Beat; Afro-Beat; Afrobeats |
| `gospel` | Gospel Music; Christian; Contemporary Christian; Spiritual; Praise & Worship |
| `new-age` | NewAge; New Age Music; Meditation; Meditative; Relaxation |
| `easy-listening` | Lounge Music; Lounge; Adult Contemporary; Muzak; Light Music |
| `experimental` | Experimental Music; Avant-Garde; Avantgarde; Avant Garde; Abstract; Musique Concrete |
| `noise` | Noise Music; Harsh Noise; Power Electronics |
| `instrumental` | Instrumental Music; Instrumental Pop; Instrumental Rock |
| `a-cappella` | Acapella; A capella; Vocal Group |
| `spoken-word` | Speech; Audiobook; Audio Book; Audio Theatre; Podcast; Spoken; Poetry |
| `comedy` | Comedy Music; Humor; Humour; Satire; Stand-Up; Standup |
| `children` | Children's Music; Kids; Children; Kids Music; Lullaby |

## ID3v1 genre numbers (0-191)

Numbers 0-79 are the original ID3v1 list, 80-191 the Winamp extensions. An empty "maps to" means: no sensible genre in our vocabulary, the value is ignored.

| # | ID3v1 name | maps to |
|---|---|---|
| 0 | Blues | `blues` |
| 1 | Classic Rock | `rock` |
| 2 | Country | `country` |
| 3 | Dance | `dance` |
| 4 | Disco | `disco` |
| 5 | Funk | `funk` |
| 6 | Grunge | `grunge` |
| 7 | Hip-Hop | `hip-hop` |
| 8 | Jazz | `jazz` |
| 9 | Metal | `metal` |
| 10 | New Age | `new-age` |
| 11 | Oldies |  |
| 12 | Other |  |
| 13 | Pop | `pop` |
| 14 | R&B | `r-and-b` |
| 15 | Rap | `rap` |
| 16 | Reggae | `reggae` |
| 17 | Rock | `rock` |
| 18 | Techno | `techno` |
| 19 | Industrial | `industrial` |
| 20 | Alternative | `alternative-rock` |
| 21 | Ska | `ska` |
| 22 | Death Metal | `death-metal` |
| 23 | Pranks |  |
| 24 | Soundtrack | `soundtrack` |
| 25 | Euro-Techno | `techno` |
| 26 | Ambient | `ambient` |
| 27 | Trip-Hop | `trip-hop` |
| 28 | Vocal |  |
| 29 | Jazz+Funk | `jazz-fusion` |
| 30 | Fusion | `jazz-fusion` |
| 31 | Trance | `trance` |
| 32 | Classical | `classical` |
| 33 | Instrumental | `instrumental` |
| 34 | Acid |  |
| 35 | House | `house` |
| 36 | Game | `soundtrack` |
| 37 | Sound Clip |  |
| 38 | Gospel | `gospel` |
| 39 | Noise | `noise` |
| 40 | Alternative Rock | `alternative-rock` |
| 41 | Bass |  |
| 42 | Soul | `soul` |
| 43 | Punk | `punk` |
| 44 | Space |  |
| 45 | Meditative | `new-age` |
| 46 | Instrumental Pop | `instrumental` |
| 47 | Instrumental Rock | `instrumental` |
| 48 | Ethnic | `world` |
| 49 | Gothic | `gothic` |
| 50 | Darkwave | `gothic` |
| 51 | Techno-Industrial | `industrial` |
| 52 | Electronic | `electronic` |
| 53 | Pop-Folk | `folk` |
| 54 | Eurodance | `dance` |
| 55 | Dream |  |
| 56 | Southern Rock | `rock` |
| 57 | Comedy | `comedy` |
| 58 | Cult |  |
| 59 | Gangsta | `hip-hop` |
| 60 | Top 40 |  |
| 61 | Christian Rap | `rap` |
| 62 | Pop/Funk | `funk` |
| 63 | Jungle | `drum-and-bass` |
| 64 | Native American | `world` |
| 65 | Cabaret |  |
| 66 | New Wave | `new-wave` |
| 67 | Psychedelic | `psychedelic-rock` |
| 68 | Rave | `techno` |
| 69 | Showtunes | `soundtrack` |
| 70 | Trailer | `soundtrack` |
| 71 | Lo-Fi | `lo-fi` |
| 72 | Tribal | `world` |
| 73 | Acid Punk | `punk` |
| 74 | Acid Jazz | `jazz` |
| 75 | Polka | `folk` |
| 76 | Retro |  |
| 77 | Musical | `soundtrack` |
| 78 | Rock & Roll | `rock` |
| 79 | Hard Rock | `hard-rock` |
| 80 | Folk | `folk` |
| 81 | Folk-Rock | `folk-rock` |
| 82 | National Folk | `folk` |
| 83 | Swing | `jazz` |
| 84 | Fast Fusion | `jazz-fusion` |
| 85 | Bebob | `jazz` |
| 86 | Latin | `latin` |
| 87 | Revival |  |
| 88 | Celtic | `folk` |
| 89 | Bluegrass | `country` |
| 90 | Avantgarde | `experimental` |
| 91 | Gothic Rock | `gothic` |
| 92 | Progressive Rock | `progressive-rock` |
| 93 | Psychedelic Rock | `psychedelic-rock` |
| 94 | Symphonic Rock | `progressive-rock` |
| 95 | Slow Rock | `rock` |
| 96 | Big Band | `jazz` |
| 97 | Chorus |  |
| 98 | Easy Listening | `easy-listening` |
| 99 | Acoustic | `folk` |
| 100 | Humour | `comedy` |
| 101 | Speech | `spoken-word` |
| 102 | Chanson |  |
| 103 | Opera | `opera` |
| 104 | Chamber Music | `classical` |
| 105 | Sonata | `classical` |
| 106 | Symphony | `classical` |
| 107 | Booty Bass | `hip-hop` |
| 108 | Primus |  |
| 109 | Porn Groove |  |
| 110 | Satire | `comedy` |
| 111 | Slow Jam | `r-and-b` |
| 112 | Club | `dance` |
| 113 | Tango | `latin` |
| 114 | Samba | `latin` |
| 115 | Folklore | `folk` |
| 116 | Ballad | `pop` |
| 117 | Power Ballad | `rock` |
| 118 | Rhythmic Soul | `soul` |
| 119 | Freestyle |  |
| 120 | Duet |  |
| 121 | Punk Rock | `punk` |
| 122 | Drum Solo |  |
| 123 | A Cappella | `a-cappella` |
| 124 | Euro-House | `house` |
| 125 | Dance Hall | `reggae` |
| 126 | Goa | `trance` |
| 127 | Drum & Bass | `drum-and-bass` |
| 128 | Club-House | `house` |
| 129 | Hardcore | `hardcore` |
| 130 | Terror |  |
| 131 | Indie | `indie` |
| 132 | BritPop | `britpop` |
| 133 | Afro-Punk | `punk` |
| 134 | Polsk Punk | `punk` |
| 135 | Beat |  |
| 136 | Christian Gangsta Rap | `rap` |
| 137 | Heavy Metal | `heavy-metal` |
| 138 | Black Metal | `black-metal` |
| 139 | Crossover |  |
| 140 | Contemporary Christian | `gospel` |
| 141 | Christian Rock | `rock` |
| 142 | Merengue | `latin` |
| 143 | Salsa | `salsa` |
| 144 | Thrash Metal | `thrash-metal` |
| 145 | Anime | `anime` |
| 146 | JPop | `j-pop` |
| 147 | Synthpop | `synth-pop` |
| 148 | Abstract | `experimental` |
| 149 | Art Rock | `progressive-rock` |
| 150 | Baroque | `classical` |
| 151 | Bhangra | `world` |
| 152 | Big Beat | `electronic` |
| 153 | Breakbeat | `electronic` |
| 154 | Chillout | `chillout` |
| 155 | Downtempo | `downtempo` |
| 156 | Dub | `dub` |
| 157 | EBM | `industrial` |
| 158 | Eclectic |  |
| 159 | Electro | `electronic` |
| 160 | Electroclash | `electronic` |
| 161 | Emo | `emo` |
| 162 | Experimental | `experimental` |
| 163 | Garage | `electronic` |
| 164 | Global | `world` |
| 165 | IDM | `electronic` |
| 166 | Illbient | `ambient` |
| 167 | Industro-Goth | `industrial` |
| 168 | Jam Band | `rock` |
| 169 | Krautrock | `progressive-rock` |
| 170 | Leftfield | `electronic` |
| 171 | Lounge | `easy-listening` |
| 172 | Math Rock | `progressive-rock` |
| 173 | New Romantic | `new-wave` |
| 174 | Nu-Breakz | `electronic` |
| 175 | Post-Punk | `post-punk` |
| 176 | Post-Rock | `post-rock` |
| 177 | Psytrance | `trance` |
| 178 | Shoegaze | `shoegaze` |
| 179 | Space Rock | `progressive-rock` |
| 180 | Trop Rock | `rock` |
| 181 | World Music | `world` |
| 182 | Neoclassical | `classical` |
| 183 | Audiobook | `spoken-word` |
| 184 | Audio Theatre | `spoken-word` |
| 185 | Neue Deutsche Welle | `new-wave` |
| 186 | Podcast | `spoken-word` |
| 187 | Indie Rock | `indie` |
| 188 | G-Funk | `hip-hop` |
| 189 | Dubstep | `dubstep` |
| 190 | Garage Rock | `rock` |
| 191 | Psybient | `ambient` |
