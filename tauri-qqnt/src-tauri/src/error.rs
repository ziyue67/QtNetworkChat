use serde::Serialize;
use serde_json::Value;

#[derive(Debug, Clone, Serialize)]
#[serde(rename_all = "camelCase")]
pub struct QQNTError {
    pub code: String,
    pub message: String,
    pub source: String,
    #[serde(skip_serializing_if = "Option::is_none")]
    pub details: Option<Value>,
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
            details: None,
        }
    }

    pub fn engine(message: impl Into<String>) -> Self {
        Self::new("engine_error", message, "engine")
    }

    pub fn rust(code: impl Into<String>, message: impl Into<String>) -> Self {
        Self::new(code, message, "rust")
    }

    pub fn with_details(mut self, details: Value) -> Self {
        self.details = Some(details);
        self
    }
}

impl std::fmt::Display for QQNTError {
    fn fmt(&self, formatter: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        write!(formatter, "{}: {}", self.code, self.message)
    }
}

impl std::error::Error for QQNTError {}

#[cfg(test)]
mod tests {
    use super::*;

    use serde_json::json;

    #[test]
    fn serializes_without_empty_details() {
        let error = QQNTError::rust("redis_connect_timeout", "Redis connection timed out.");
        let value = serde_json::to_value(error).expect("QQNTError should serialize");

        assert_eq!(value["code"], "redis_connect_timeout");
        assert_eq!(value["message"], "Redis connection timed out.");
        assert_eq!(value["source"], "rust");
        assert!(value.get("details").is_none());
    }

    #[test]
    fn serializes_optional_details() {
        let error = QQNTError::rust("redis_unavailable", "Redis is unavailable for QQNTServer.")
            .with_details(json!({
                "host": "127.0.0.1",
                "port": 6379
            }));
        let value = serde_json::to_value(error).expect("QQNTError should serialize");

        assert_eq!(value["code"], "redis_unavailable");
        assert_eq!(value["details"]["host"], "127.0.0.1");
        assert_eq!(value["details"]["port"], 6379);
    }
}
