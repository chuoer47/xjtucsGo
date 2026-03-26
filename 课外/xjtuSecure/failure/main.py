import requests
import json
from requests.exceptions import RequestException
from json.decoder import JSONDecodeError

from config import headers, choice2num
from extract_info import extract_info
from get_ans import get_ans
from get_keys import get_keys
from submit_ans import submit_ans
from datetime import datetime, timedelta

# 获取当前时间并格式化为指定格式
current_time = datetime.now()
starttime = current_time.strftime("%Y-%m-%d %H:%M:%S")

base_url = "http://10.184.203.130/xjtu_ksxt/Center/Exam/beginExam.html"
params = {
    "t": "",
    "examination_no": "744"
}


def get_exam_json():
    """获取包含JSON数据的考试页面内容"""

    try:
        response = requests.get(
            url=base_url,
            params=params,
            headers=headers,
            timeout=10,
            verify=False
        )
        response.raise_for_status()
        # 确保正确解码（处理Unicode转义的关键步骤）
        response.encoding = response.apparent_encoding
        return response.text
    except RequestException as e:
        print(f"请求失败: {str(e)}")
        return None


def parse_json_content(json_str):
    """解析JSON字符串，自动处理Unicode转义"""
    if not json_str:
        return None

    try:
        # json.loads会自动将\uXXXX转义字符转换为对应中文
        json_data = json.loads(json_str)
        return json_data
    except JSONDecodeError as e:
        print(f"JSON解析错误: {str(e)}")
        # 尝试处理可能包含的多余字符（如前后有非JSON内容）
        try:
            # 简单截取可能的JSON部分（根据实际情况调整）
            start = json_str.find('{')
            end = json_str.rfind('}') + 1
            if start != -1 and end != 0:
                fixed_json = json_str[start:end]
                json_data = json.loads(fixed_json)
                print("修复后JSON解析成功")
                return json_data
            else:
                print("无法提取有效的JSON部分")
                return None
        except Exception as e2:
            print(f"修复尝试失败: {str(e2)}")
            return None
    except Exception as e:
        print(f"解析过程出错: {str(e)}")
        return None


def main():
    # 获取页面内容
    json_content = get_exam_json()
    if not json_content:
        print("请求失败")
        return

    # 解析JSON数据（自动处理Unicode转义）
    parsed_data = parse_json_content(json_content)
    data_list = parsed_data['data']['paper']['list']
    paper_no = parsed_data["data"]["paper"]["paperInfo"]["paper_no"]

    # 通过题目详细列表获取精简的信息
    data_refine_list = extract_info(data_list)
    # 添加答案，可以使用线程池优化
    for i, question in enumerate(data_refine_list):
        ans_json = parse_json_content(get_ans(question["question_no"]))
        ans = ans_json["data"]["answer"]
        question["question_type"] = ans_json["data"]["question_type"]
        question["ans"] = ans

    # 获取题单
    keys = get_keys()

    # 构建答案的表单
    examination_no = params["examination_no"]

    form_data = {}
    form_data["examination_no"] = examination_no
    form_data["paper_no"] = paper_no
    form_data["starttime"] = "2025-09-15 17:29:50"
    form_data["answer"] = []
    form_data["list_question"] = {}
    form_data["list_score"] = {}

    # 构建表单的正确答案
    for i in range(len(data_refine_list)):
        question = data_refine_list[i]

        # 生成映射
        keys_list_no = keys[i]
        list_no = question["list_no"]
        # form_data["list_question"][keys_list_no] = list_no

        # 生成得分
        # form_data["list_score"][keys_list_no] = "1"

        # 生成答案
        d = {}
        d["list_no"] = list_no
        ans = question["ans"]
        if ans in "ABCD":
            ans = [question["XUANXIANG"][choice2num[ans]]]
        elif ans in ["正确", "错误"]:
            ans = ["1" if ans == "正确" else "0"]
        else:
            ans = ["1"]
        d["answer"] = ans
        form_data["answer"].append(d)

    print(form_data)
    submit_ans(form_data)


if __name__ == "__main__":
    main()
