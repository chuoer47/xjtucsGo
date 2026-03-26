from parse_json import parse_json_content

ans_base_url = "http://10.184.203.130/xjtu_ksxt/Center/Question/getQuestionDetail.html?question_no="

# headers中的cookie需要配置
headers = {
    "Cookie": "<plz input your cookie>",
    "Accept": "text/html,application/xhtml+xml,application/xml;q=0.9,image/avif,image/webp,image/apng,*/*;q=0.8,application/signed-exchange;v=b3;q=0.7",
    "Accept-Encoding": "gzip, deflate",
    "Accept-Language": "zh-CN,zh;q=0.9,en;q=0.8,en-GB;q=0.7,en-US;q=0.6",
    "Cache-Control": "no-cache",
    "Connection": "keep-alive",
    "Host": "10.184.203.130",
    "Pragma": "no-cache",
    "Upgrade-Insecure-Requests": "1",
    "User-Agent": "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/127.0.0.0 Safari/537.36 Edg/127.0.0.0"
}

# 按照README.md的指示复制到 context.txt文件即可
with open("context.txt", "r", encoding="utf-8") as f:
    content = f.read()
context = parse_json_content(content)

