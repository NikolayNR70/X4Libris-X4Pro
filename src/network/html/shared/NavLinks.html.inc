<!-- The navigation bar every page shows. A page includes this where its bar
     goes, and shared/NavLinks.css.inc with its styles; a new page is added here
     once instead of to every page. No tag names in this comment: the minifier
     would take one for a real tag. -->
<nav class="nav-links">
  <a href="/files">Файлы</a>
  <a href="/settings">Настройки</a>
  <a href="/fonts">Шрифты</a>
  <a href="/dictionary">Словари</a>
  <a href="/stats">Статистика чтения</a>
  <a href="/plugins">Плагины</a>
  <a href="/systeminfo">Сведения о системе</a>
  <a href="/diagnostics">Диагностика</a>
</nav>
<script>
  // Highlight the page being shown. Runs inline, so before the first paint; the
  // start page at / matches no link.
  for (const link of document.currentScript.previousElementSibling.querySelectorAll('a')) {
    if (link.getAttribute('href') === location.pathname) link.classList.add('active');
  }
</script>
