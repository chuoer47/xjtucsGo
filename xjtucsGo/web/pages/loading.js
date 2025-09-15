import React from 'react';
import styles from '../styles/Loading.module.css';

const Loading = () => {
  return (
    <div className={styles.loadingContainer}>
      <div className={styles.loadingSpinner}>
        <div className={styles.spinner}></div>
        <p className={styles.loadingText}>加载中...</p>
      </div>
    </div>
  );
};

export default Loading; 