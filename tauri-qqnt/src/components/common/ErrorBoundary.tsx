import { Component, type ErrorInfo, type ReactNode } from 'react'

interface ErrorBoundaryProps {
  children: ReactNode
  fallback?: ReactNode
  resetKey?: string
}

interface ErrorBoundaryState {
  hasError: boolean
  message?: string
}

export class ErrorBoundary extends Component<ErrorBoundaryProps, ErrorBoundaryState> {
  state: ErrorBoundaryState = { hasError: false }

  static getDerivedStateFromError(error: Error): ErrorBoundaryState {
    return { hasError: true, message: error.message }
  }

  componentDidUpdate(prevProps: ErrorBoundaryProps) {
    if (this.state.hasError && prevProps.resetKey !== this.props.resetKey) {
      this.setState({ hasError: false, message: undefined })
    }
  }

  componentDidCatch(error: Error, errorInfo: ErrorInfo) {
    console.error('QQ NT view crashed', error, errorInfo)
  }

  render() {
    if (this.state.hasError) {
      return this.props.fallback ?? (
        <div className="flex h-full flex-col items-center justify-center gap-3 bg-[var(--qq-bg)] px-6 text-center">
          <div className="text-base font-semibold text-[var(--qq-text)]">当前页面加载失败</div>
          <p className="max-w-md text-sm text-[var(--qq-text-secondary)]">
            页面数据异常，客户端仍可继续操作；可以切换到其他入口或重试当前页面。
          </p>
          {this.state.message ? (
            <p className="max-w-md break-all rounded bg-[var(--qq-bg-tertiary)] px-3 py-2 text-xs text-[var(--qq-text-tertiary)]">
              {this.state.message}
            </p>
          ) : null}
          <button
            type="button"
            onClick={() => this.setState({ hasError: false, message: undefined })}
            className="rounded-md bg-[var(--qq-primary)] px-4 py-2 text-sm font-medium text-white hover:bg-[var(--qq-primary-hover)]"
          >
            重试
          </button>
        </div>
      )
    }

    return this.props.children
  }
}
