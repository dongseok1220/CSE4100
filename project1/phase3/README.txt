[system programming lecture]

-project 1 baseline

csapp.{c,h}
        CS:APP3e functions

shellex.c
        Simple shell example

20191619 이동석

1. 컴파일
make 명령어를 입력하면 myshell 실행파일이 생성된다. 

2. myshell 프로그램에서는 다음과 같은 동작이 가능하며 실행시 히스토리 로그를 저장할 파일이 홈 디렉토리에 생성된다. 
cd : 쉘에 존재하는 디렉터리 탐색
history : 사용자가 입력한 명령어 출력
!! / !# : 가장 최근에 입력한 명령어 / #번호에 해당하는 명령어 실행 혹은 대치
ls : 디렉터리 내용물 출력
mkdir : 디렉터리 생성
rmdir : 디렉터리 삭제
touch : 파일 생성
cat : 파일 내용 읽기
echo : 파일 내용 출력
exit : 쉘 종료
quit : 쉘 종료

3. pipe 명령어를 처리하는 기능이 추가되었다. 파이프의 개수 제한은 없다. 
ex)
 ls -al | grep filename 
cat filename | grep -i "test" 
...

4. 프로세스를 관리할 수 있는 명령어 및 시그널 핸들러가 추가 구현되었다. 
• jobs: List the running and stopped background jobs.
• bg ⟨job⟩: Change a stopped background job to a running background job.
• fg ⟨job⟩: Change a stopped or running a background job to a running in the foreground.
• kill ⟨job⟩: Terminate a job

bg/fg/kill 은 모두 %#의 형태로 입력받는다. 

사용자는 실행중인 프로세스에서 ctrl+z를 누르면 해당 프로세스를 중지시킬 수 있다.
사용자는 실행중인 프로세스에서 ctrl+c를 누르면 해당 프로세스를 종료시킬 수 있다. 
