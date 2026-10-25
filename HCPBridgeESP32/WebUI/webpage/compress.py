Import("env")
import importlib.util
from pathlib import Path
project = Path(env["PROJECT_DIR"])
spec = importlib.util.spec_from_file_location("hcp_web_assets", project / "WebUI/generate_assets.py")
module = importlib.util.module_from_spec(spec)
spec.loader.exec_module(module)
module.generate(project)
print("Embedded web pages and language JSON generated")
