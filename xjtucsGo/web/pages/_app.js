import '../styles/glassmorphism.css';
import { useState, useEffect } from 'react';
import Loading from './loading';

function MyApp({ Component, pageProps }) {
  const [loading, setLoading] = useState(true);

  useEffect(() => {
    // 模拟加载时间，实际使用时可以根据需要调整
    const timer = setTimeout(() => {
      setLoading(false);
    }, 2000);

    return () => clearTimeout(timer);
  }, []);

  return (
    <>
      {loading ? (
        <Loading />
      ) : (
        <Component {...pageProps} />
      )}
    </>
  );
}

export default MyApp; 