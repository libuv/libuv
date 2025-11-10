document.addEventListener('DOMContentLoaded', function() {
  const codeBlocks = document.querySelectorAll('div.highlight');
  
  codeBlocks.forEach(function(codeBlock) {
    const parent = codeBlock.parentElement;
    
    if (parent && parent.classList.contains('code-block-wrapper')) {
      return;
    }
    
    const button = document.createElement('button');
    button.className = 'copy-button';
    button.type = 'button';
    button.setAttribute('aria-label', 'Copy code to clipboard');
    button.innerHTML = '<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round"><rect x="9" y="9" width="13" height="13" rx="2" ry="2"></rect><path d="M5 15H4a2 2 0 0 1-2-2V4a2 2 0 0 1 2-2h9a2 2 0 0 1 2 2v1"></path></svg>';
    
    button.addEventListener('click', function() {
      const pre = codeBlock.querySelector('pre');
      const code = pre ? pre.textContent : codeBlock.textContent;
      
      navigator.clipboard.writeText(code).then(function() {
        button.classList.add('copied');
        button.innerHTML = '<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round"><polyline points="20 6 9 17 4 12"></polyline></svg>';
        button.setAttribute('aria-label', 'Code copied to clipboard');
        
        setTimeout(function() {
          button.classList.remove('copied');
          button.innerHTML = '<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round"><rect x="9" y="9" width="13" height="13" rx="2" ry="2"></rect><path d="M5 15H4a2 2 0 0 1-2-2V4a2 2 0 0 1 2-2h9a2 2 0 0 1 2 2v1"></path></svg>';
          button.setAttribute('aria-label', 'Copy code to clipboard');
        }, 2000);
      }).catch(function(err) {
        console.error('Failed to copy code: ', err);
      });
    });
    
    if (parent && (parent.classList.contains('highlight-default') || parent.classList.contains('highlight-c') || parent.classList.contains('highlight-python'))) {
      parent.style.position = 'relative';
      parent.appendChild(button);
    } else {
      const wrapper = document.createElement('div');
      wrapper.className = 'code-block-wrapper';
      codeBlock.parentNode.insertBefore(wrapper, codeBlock);
      wrapper.appendChild(button);
      wrapper.appendChild(codeBlock);
    }
  });
});
