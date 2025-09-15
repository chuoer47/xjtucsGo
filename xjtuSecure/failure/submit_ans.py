import requests

from config import headers

end_url = "http://10.184.203.130/xjtu_ksxt/Center/Exam/endExam.html"
params = {
    "t": ""
}


def submit_ans(form_data):
    response = requests.post(
        end_url,
        params=params,
        data=form_data,
        headers=headers
    )
    # 4. 处理响应
    print("响应状态码:", response.status_code)
    print("响应内容:", response.text)
    return int(response.status_code) == 200
