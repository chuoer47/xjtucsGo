import subprocess
import json
import yaml
from pathlib import Path


def call_zepp_life_js(account, password, steps):
    """调用ZeppLifeSteps.js，修复文件路径引用问题"""
    try:
        cmd = [
            "node",
            "-e",
            """
            const zepp = require('./ZeppLifeSteps.js');
            async function run() {
                try {
                    const { loginToken, userId } = await zepp.login('%s', '%s');
                    const appToken = await zepp.getAppToken(loginToken);
                    const result = await zepp.updateSteps(loginToken, appToken, %d);
                    console.log(JSON.stringify({
                        success: true,
                        message: '步数更新成功',
                        data: result
                    }));
                } catch (error) {
                    console.log(JSON.stringify({
                        success: false,
                        message: error.message,
                        error: error.toString(),
                        status: error.response?.status
                    }));
                }
            }
            run();
            """ % (account, password, steps)
        ]

        result = subprocess.run(
            cmd,
            capture_output=True,
            text=True,
            encoding='utf-8',
            check=True
        )

        # 提取最后一行JSON
        output_lines = result.stdout.strip().split('\n')
        json_str = None
        for line in reversed(output_lines):
            line = line.strip()
            if line.startswith('{') and line.endswith('}'):
                json_str = line
                break

        if not json_str:
            return {
                "success": False,
                "message": "未找到有效的JSON响应"
            }

        return json.loads(json_str)

    except subprocess.CalledProcessError as e:
        return {
            "success": False,
            "message": "命令执行失败",
            "error": e.stderr
        }
    except json.JSONDecodeError as e:
        return {
            "success": False,
            "message": "JSON解析失败",
            "error": str(e),
            "output": result.stdout if 'result' in locals() else None
        }
    except Exception as e:
        return {
            "success": False,
            "message": str(e),
            "type": type(e).__name__
        }


def load_config(config_path="config.yml"):
    """加载YAML配置文件"""
    try:
        if not Path(config_path).exists():
            raise FileNotFoundError(f"配置文件 {config_path} 不存在")
        with open(config_path, 'r', encoding='utf-8') as f:
            config = yaml.safe_load(f)
        return config
    except Exception as e:
        print(f"加载配置文件失败: {str(e)}")
        return None


# 使用示例
if __name__ == "__main__":
    config = load_config()
    if config:
        ACCOUNT = config["ACCOUNT"]
        PASSWORD = config["PASSWORD"]
        TARGET_STEPS = config["TARGET_STEPS"]
        result = call_zepp_life_js(ACCOUNT, PASSWORD, TARGET_STEPS)
        print(json.dumps(result, indent=2, ensure_ascii=False))
