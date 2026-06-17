use serde::Serialize;

#[derive(Debug, Clone, Serialize)]
#[serde(rename_all = "camelCase")]
pub struct QQNTError {
    pub code: String,
    pub message: String,
    pub source: String,
}

pub type QQNTResult<T> = Result<T, QQNTError>;

impl QQNTError {
    pub fn new(
        code: impl Into<String>,
        message: impl Into<String>,
        source: impl Into<String>,
    ) -> Self {
        Self {
            code: code.into(),
            message: message.into(),
            source: source.into(),
        }
    }

    pub fn engine(message: impl Into<String>) -> Self {
        Self::new("engine_error", message, "engine")
    }

    pub fn rust(code: impl Into<String>, message: impl Into<String>) -> Self {
        Self::new(code, message, "rust")
    }
}

impl std::fmt::Display for QQNTError {
    fn fmt(&self, formatter: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        write!(formatter, "{}: {}", self.code, self.message)
    }
}

impl std::error::Error for QQNTError {}
