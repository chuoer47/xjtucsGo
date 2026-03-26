import requests
from requests import RequestException

from config import headers, ans_base_url


def get_ans(no):
    """获取包含JSON数据的考试页面内容"""
    url = ans_base_url + str(no)
    try:
        response = requests.get(
            url=url,
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