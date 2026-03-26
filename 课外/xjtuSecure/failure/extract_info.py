def extract_info(original_data):
    """
    提取题目信息，转换XUANXIANG为选项文字列表
    :param original_data: 包含原始题目数据的字典
    :return: 提取后的题目列表
    """
    extracted_list = []

    # 遍历原始数据中的"list"字典（假设数据顶层有"list"键）
    for _,item in original_data.items():
        # 提取基础字段
        list_no = item.get("list_no")
        question_no = item.get("question_no")
        question_stem = item.get("question_stem")

        # 处理XUANXIANG，提取所有choice的文字组成列表
        options = [opt.get("choice", "") for opt in item.get("XUANXIANG", [])]

        # 组合成新的字典并添加到结果列表
        extracted_list.append({
            "list_no": list_no,
            "question_no": question_no,
            "question_stem":question_stem,
            "XUANXIANG": options  # 仅包含选项文字的列表
        })

    return extracted_list
