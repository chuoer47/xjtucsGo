import json

from requests import JSONDecodeError


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