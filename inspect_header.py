from pathlib import Path
path = Path(r"D:\C++VS pro\QtNetworkChat\src\views\messagesview.cpp")
text = path.read_text(encoding="utf-8")
idx = text.find("m_chatTitleLabel")
print("Found m_chatTitleLabel at", idx)
print("--- repr snippet ---")
print(repr(text[idx-100:idx+900]))
