from config import context
from xjtuSecure.auxiliary.extract_info import extract_info
from xjtuSecure.auxiliary.get_ans import get_ans
from xjtuSecure.auxiliary.parse_json import parse_json_content


def process_questions(json_data, output_file="ans.txt"):
    """
    处理JSON格式的题库数据并写入TXT文件

    参数:
        json_data: 包含题库数据的列表
        output_file: 输出的TXT文件名，默认为ans.txt
    """
    # 准备输出内容
    output_lines = []

    # 遍历每个题目
    for i, question in enumerate(json_data, 1):
        # 添加题目编号
        output_lines.append(f"{'#' * 40}")
        output_lines.append(f"题目 {i}")
        output_lines.append(f"{'#' * 40}")

        # 添加基本信息
        output_lines.append(f"题库编号: {question.get('list_no', '未知')}")
        output_lines.append(f"题目编号: {question.get('question_no', '未知')}")
        output_lines.append(f"题目类型: {question.get('question_type', '单选题')}")
        output_lines.append("")

        # 添加题干
        output_lines.append(f"题干: {question.get('question_stem', '')}")
        output_lines.append("")

        # 添加选项
        output_lines.append("选项:")
        options = question.get('XUANXIANG', [])
        # 选项字母A、B、C、D
        option_labels = ['A', 'B', 'C', 'D', 'E', 'F', 'G', 'H']

        for j, option in enumerate(options):
            if j < len(option_labels):
                output_lines.append(f"  {option_labels[j]}: {option}")

        output_lines.append("")

        # 添加答案
        answer = question.get('ans', '未提供')
        # 将答案转换为对应的选项文字（如果可能）
        try:
            ans_index = option_labels.index(answer)
            if ans_index < len(options):
                output_lines.append(f"正确答案: {answer} ({options[ans_index]})")
            else:
                output_lines.append(f"正确答案: {answer}")
        except (ValueError, IndexError):
            output_lines.append(f"正确答案: {answer}")

        # 添加分隔线
        output_lines.append("\n" + "-" * 40 + "\n")

    # 写入文件
    with open(output_file, 'w', encoding='utf-8') as f:
        f.write('\n'.join(output_lines))

    print(f"处理完成，已写入文件: {output_file}")


def main():
    # 通过题目详细列表获取精简的信息
    data_refine_list = extract_info(context)
    # 添加答案，可以使用线程池优化
    for i, question in enumerate(data_refine_list):
        ans_json = parse_json_content(get_ans(question["question_no"]))
        ans = ans_json["data"]["answer"]
        question["question_type"] = ans_json["data"]["question_type"]
        question["ans"] = ans
    process_questions(data_refine_list)


if __name__ == "__main__":
    main()
