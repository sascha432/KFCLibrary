<?php

namespace Json2php\Console;

use Json2php\GeneratorESPWebFramework;
use Symfony\Component\Console\Command\Command;
use Symfony\Component\Console\Input\InputArgument;
use Symfony\Component\Console\Input\InputInterface;
use Symfony\Component\Console\Input\InputOption;
use Symfony\Component\Console\Output\OutputInterface;

class GeneratorESPWebFramekworkCommand extends Command
{
    protected function configure()
    {
        $this->setName('generator:esp-web-framework')
            ->setAliases(['g:ewf'])
            ->setDescription('Generate a generic php object file containing properties, get and set methods')
            ->setHelp('This command can generate a generic php object')
            ->addOption('type', 't', InputOption::VALUE_OPTIONAL, 'Json source, file or json string, value in [file | string]', 'file')
            ->addOption('php_out', 'o', InputOption::VALUE_OPTIONAL, 'Generate php file storage path, default to current directory', './')
            ->addArgument('json', InputArgument::REQUIRED, 'Json file path or json string');
    }

    protected function execute(InputInterface $input, OutputInterface $output)
    {
        $type = $input->getOption('type');
        $json = $input->getArgument('json');
        switch ($type) {
            case 'file':
                if (!is_file($json)) {
                    $output->writeln($json . ' is an invalid path');
                    exit();
                }
                $json = file_get_contents($json);
                if (!$this->validatorJson($json)) {
                    $output->writeln('Invalid json, please enter the correct json string');
                    exit();
                }
                break;
            case 'string':
                if (!$this->validatorJson($json)) {
                    $output->writeln('Invalid json, please enter the correct json string');
                    exit();
                }
                break;
            default:
                $output->writeln('Invalid json type');
                exit();
        }
        (new GeneratorESPWebFramework())->generator(
            $json,
            'JsonConfiguration',
            'ESPWebFramework\JsonConfiguration',
            $input->getOption('php_out')
        );
    }

    private function validatorJson($json)
    {
        json_decode($json);
        if (json_last_error()) {
            json_last_error_msg();
            return false;
        }
        return true;
    }
}