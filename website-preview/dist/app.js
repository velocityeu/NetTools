'use strict';
// Lesson content is generated from docs/help/topics.json.
const topics = document.getElementById('topics');
const article = document.getElementById('help-article');
let current = 0;
let matches = articles.map((_, index) => index);
function showArticle(index, focus = false) {
 current = index;
 const heading = document.createElement('h3'); heading.id = 'lesson-title'; heading.tabIndex = -1;
 heading.textContent = articles[index].title;
 article.replaceChildren(heading);
 article.insertAdjacentHTML('beforeend', articles[index].body);
 topics.querySelectorAll('button').forEach(button => button.setAttribute('aria-current', String(Number(button.dataset.index) === index)));
 const position = matches.indexOf(index);
 const pager = document.createElement('div'); pager.className = 'article-pager';
 [['Previous lesson', position - 1], ['Next lesson', position + 1]].forEach(([label, nextPosition]) => {
  const button = document.createElement('button'); button.textContent = label;
  button.disabled = nextPosition < 0 || nextPosition >= matches.length;
  button.addEventListener('click', () => showArticle(matches[nextPosition], true));
  pager.append(button);
 });
 article.append(pager);
 if (focus) document.getElementById('lesson-title').focus();
}
articles.forEach((item, index) => {
 const button = document.createElement('button'); button.className = 'topic';
 button.textContent = item.title; button.dataset.index = String(index);
 button.addEventListener('click', () => showArticle(index, true)); topics.append(button);
});
const search = document.getElementById('help-search');
function filterLessons() {
 const query = search.value.trim().toLowerCase(); matches = [];
 topics.querySelectorAll('button').forEach((button, index) => {
  const item = articles[index];
  button.hidden = !(item.title + ' ' + item.keywords + ' ' + item.body.replace(/<[^>]*>/g, ' ')).toLowerCase().includes(query);
  if (!button.hidden) matches.push(index);
 });
 document.getElementById('search-status').textContent = matches.length + (matches.length === 1 ? ' lesson' : ' lessons') + (query ? ' matching your search' : ' available');
 document.getElementById('no-topics').hidden = matches.length > 0;
 if (matches.length) showArticle(matches.includes(current) ? current : matches[0]);
 else {
  topics.querySelectorAll('button').forEach(button => button.setAttribute('aria-current', 'false'));
  article.innerHTML = '<h3 id="lesson-title">No matching lessons</h3><p>Try another term or clear the search to browse all lessons.</p>';
 }
}
search.addEventListener('input', filterLessons);
document.getElementById('clear-search').addEventListener('click', () => { search.value = ''; filterLessons(); search.focus(); });
filterLessons();
const dialog=document.getElementById('about-dialog');
document.querySelectorAll('[data-about]').forEach(button=>button.addEventListener('click',()=>dialog.showModal()));
document.getElementById('close-about').addEventListener('click',()=>dialog.close());
document.getElementById('about-ok').addEventListener('click',()=>dialog.close());
document.getElementById('about-help').addEventListener('click',()=>{dialog.close();document.getElementById('help').scrollIntoView();document.getElementById('help-search').focus({preventScroll:true});});
document.getElementById('practice-form').addEventListener('submit',event=>{event.preventDefault();const answer=new FormData(event.currentTarget).get('answer');document.getElementById('practice-result').textContent=(answer==='64'?'Correct. ':'Not quite. ')+ 'The blocks start at 0, 32, 64 and 96. Since 77 is in 64–95, the network is 192.168.5.64/27. Broadcast: .95. Usable hosts: .65–.94 (30 addresses).';});
document.getElementById('practice-form').addEventListener('change', () => { document.getElementById('practice-result').textContent = ''; });
