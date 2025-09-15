import re


def get_keys():
    """
    获取系统得到的随机映射下标
    """
    data = """
    list_question[329986]: 208295
    list_question[337421]: 210787
    list_question[389880]: 208297
    list_question[407312]: 208313
    list_question[414936]: 210781
    list_question[468943]: 207363
    list_question[483099]: 208314
    list_question[493596]: 214887
    list_question[508072]: 208335
    list_question[511813]: 207195
    list_question[527023]: 210747
    list_question[528386]: 208207
    list_question[570394]: 208337
    list_question[626589]: 207130
    list_question[700487]: 207129
    list_question[703501]: 208330
    list_question[715917]: 209660
    list_question[720947]: 207304
    list_question[758345]: 207220
    list_question[771726]: 208341
    list_question[772353]: 208328
    list_question[778055]: 210717
    list_question[791332]: 208309
    list_question[793124]: 208311
    list_question[800829]: 208339
    list_question[817889]: 207237
    list_question[877748]: 208306
    list_question[884396]: 208351
    list_question[889917]: 207377
    list_question[892436]: 207194
    list_question[957748]: 207471
    list_question[959969]: 208290
    list_question[965953]: 207218
    list_question[982945]: 207213
    list_question[985383]: 208304
    list_question[986065]: 207368
    list_question[986291]: 208336
    list_question[989751]: 210789
    list_question[993653]: 207183
    list_question[995239]: 210856
    list_question[997172]: 205260
    list_question[1001613]: 210833
    list_question[1008247]: 208800
    list_question[1019886]: 208345
    list_question[1027149]: 208346
    list_question[1036741]: 210801
    list_question[1056474]: 208316
    list_question[1110387]: 208291
    list_question[1124092]: 207531
    list_question[1138786]: 210839
    list_question[1158738]: 208349
    list_question[1163651]: 209721
    list_question[1167546]: 207216
    list_question[1184324]: 205264
    list_question[1188493]: 207241
    list_question[1202113]: 207466
    list_question[1213402]: 210764
    list_question[1226524]: 207369
    list_question[1226730]: 209696
    list_question[1250394]: 207156
    list_question[1253675]: 207520
    list_question[1259224]: 208347
    list_question[1264041]: 208354
    list_question[1264235]: 207287
    list_question[1273494]: 207318
    list_question[1323853]: 207330
    list_question[1331271]: 207185
    list_question[1352102]: 208279
    list_question[1360902]: 207155
    list_question[1392496]: 207157
    list_question[1402208]: 208218
    list_question[1412504]: 207518
    list_question[1431573]: 208785
    list_question[1435914]: 207153
    list_question[1445488]: 209694
    list_question[1446759]: 208340
    list_question[1451609]: 208327
    list_question[1453187]: 207333
    list_question[1468100]: 204232
    list_question[1474167]: 209684
    list_question[1491256]: 208266
    list_question[1505522]: 207336
    list_question[1510952]: 209663
    list_question[1564638]: 207212
    list_question[1568875]: 207141
    list_question[1631574]: 207305
    list_question[1632405]: 208318
    list_question[1651730]: 207217
    list_question[1679017]: 210855
    list_question[1683928]: 207322
    list_question[1694205]: 210815
    list_question[1708928]: 207221
    list_question[1720245]: 207143
    list_question[1782571]: 208350
    list_question[1783517]: 207219
    list_question[1808454]: 207238
    list_question[1843233]: 207162
    list_question[1846504]: 207189
    list_question[1853318]: 208343
    list_question[1969296]:1212121
    """

    # 按行分割并提取键
    lines = data.strip().split('\n')
    pattern = re.compile(r'list_question\[(\d+)\]')  # 匹配方括号内的数字
    keys = []
    for line in lines:
        match = pattern.search(line)
        if match:
            keys.append(match.group(1))

    return keys
